/*
   +----------------------------------------------------------------------+
   | Copyright © TrueAsync contributors.                                  |
   +----------------------------------------------------------------------+
   | This source file is subject to the Modified BSD License that is      |
   | bundled with this package in the file LICENSE.                       |
   |                                                                      |
   | SPDX-License-Identifier: BSD-3-Clause                                |
   +----------------------------------------------------------------------+
   | Authors: Edmond <edmondifthen@proton.me>                             |
   +----------------------------------------------------------------------+
*/
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "php.h"
#include "php_true_async.h"
#include "reactor.h"
#include "scheduler.h"
#include "exceptions.h"

#ifdef HAVE_IOR
#include "main/php_io_ring.h"
#endif

#if (defined(__linux__) && defined(CLOCK_MONOTONIC_COARSE)) || defined(CLOCK_MONOTONIC_FAST)
#include <time.h>
#endif

///////////////////////////////////////////////////////////////////
/// The lists
///////////////////////////////////////////////////////////////////

static zend_always_inline void list_add(async_reactor_link_t *head, async_reactor_link_t *link)
{
	link->prev = head;
	link->next = head->next;
	head->next->prev = link;
	head->next = link;
}

static zend_always_inline void list_remove(async_reactor_link_t *link)
{
	if (UNEXPECTED(link->prev == NULL)) {
		return;
	}

	link->prev->next = link->next;
	link->next->prev = link->prev;
	link->prev = NULL;
	link->next = NULL;
}

static zend_always_inline bool list_is_empty(const async_reactor_link_t *head)
{
	return head->next == head;
}

static zend_always_inline void list_init(async_reactor_link_t *head)
{
	head->prev = head;
	head->next = head;
}

static zend_always_inline async_io_event_t *io_event_from_link(async_reactor_link_t *link)
{
	return (async_io_event_t *) ((char *) link - offsetof(async_io_event_t, reactor_link));
}

///////////////////////////////////////////////////////////////////
/// The queue
///////////////////////////////////////////////////////////////////

/* The thread's queue, created on first use as TrueAsync starts libuv lazily
 * (libuv_reactor.c:343-349): the Ring where the core has ior, the Poll queue otherwise or when the
 * Ring cannot be created. NULL with an Error. */
static php_io_queue *reactor_queue(async_reactor_t *reactor)
{
	if (EXPECTED(reactor->queue != NULL)) {
		return reactor->queue;
	}

	php_io_queue *queue = NULL;

#ifdef HAVE_IOR
#ifdef TRUE_ASYNC_TEST_HOOKS
	if (!reactor->test_poll_queue)
#endif
	{
		queue = php_io_queue_create_ring(0);
	}
#endif

	if (UNEXPECTED(queue == NULL)) {
		queue = php_io_queue_create_poll(PHP_POLL_BACKEND_AUTO);
	}

	if (UNEXPECTED(queue == NULL)) {
		zend_throw_error(NULL, "Cannot create the IO queue");
		return NULL;
	}

	reactor->queue = queue;
#ifndef PHP_WIN32
	reactor->queue_pid = getpid();
#endif

	return queue;
}

static zend_result reactor_submit(async_reactor_t *reactor, async_io_event_t *event, async_reactor_link_t *list);

/* In a forked child (S4.md 3.1): the parent's queue goes, and its waits with it, unrun; their
 * coroutines end in the child's deadlock resolution. The destroy detaches every op still on the
 * queue. The reactor's own ops go to a new queue at once, oldest first (with none, the next submit
 * creates it). An Error when one cannot: it and the ones after it stay on the list unsubmitted for
 * the rest of the request, and D16 is unbounded in that child (the Sage: a retry would cost every
 * submit a branch for a fork during the shutdown and a queue the child cannot create). */
static void reactor_rebuild(async_reactor_t *reactor)
{
	async_reactor_link_t *head = &reactor->waits;

	while (!list_is_empty(head)) {
		list_remove(head->next);
	}

	reactor->queue->ops->destroy(reactor->queue);
	reactor->queue = NULL;

	uint32_t own_count = 0;

	for (const async_reactor_link_t *link = reactor->own.next; link != &reactor->own; link = link->next) {
		own_count++;
	}

	while (own_count-- > 0) {
		async_io_event_t *event = io_event_from_link(reactor->own.prev);

		ZEND_ASSERT(event->op.queue == NULL && "the destroy detached it");
		list_remove(&event->reactor_link);

		if (UNEXPECTED(reactor_submit(reactor, event, &reactor->own) == FAILURE)) {
			list_add(&reactor->own, &event->reactor_link);
			return;
		}
	}
}

void async_reactor_request_startup(void)
{
	async_reactor_t *reactor = &ASYNC_G(reactor);

	reactor->queue = NULL;
	reactor->last_poll = 0;
	list_init(&reactor->waits);
	list_init(&reactor->own);
#ifdef TRUE_ASYNC_TEST_HOOKS
	reactor->test_poll_queue = false;
#endif
}

void async_reactor_request_shutdown(void)
{
	async_reactor_t *reactor = &ASYNC_G(reactor);
	async_reactor_link_t *head = &reactor->waits;

	while (!list_is_empty(head)) {
		async_io_event_orphan(io_event_from_link(head->next));
	}

	ZEND_ASSERT(list_is_empty(&reactor->own) && "the owners withdrew their ops");

	if (EXPECTED(reactor->queue != NULL)) {
		reactor->queue->ops->destroy(reactor->queue);
		reactor->queue = NULL;
	}
}

///////////////////////////////////////////////////////////////////
/// IO events
///////////////////////////////////////////////////////////////////

async_io_event_t *async_io_event_new(void)
{
	async_io_event_t *event = emalloc(sizeof(async_io_event_t));

	async_event_init(&event->base, 0);
	memset(&event->op, 0, sizeof(event->op));
	memset(&event->result, 0, sizeof(event->result));
	event->reactor_link.prev = NULL;
	event->reactor_link.next = NULL;

	return event;
}

void async_io_event_release(async_io_event_t *event)
{
	if (--event->base.ref_count > 0) {
		return;
	}

	ZEND_ASSERT(event->op.queue == NULL && event->reactor_link.prev == NULL && "a submitted op is withdrawn first");

	async_callbacks_free((async_awaitable_t *) event, &event->base.callbacks);
	efree(event);
}

/* A completion of the reactor's queue: the event fires once, in scheduler context (the notify sets
 * it), under a reference of its own, since the wake of its last waiter releases the record's. */
static void reactor_dispatch(const php_io_queue_completion *completion)
{
	async_io_event_t *event = completion->data;

	event->result = completion->result;
	event->base.flags |= ASYNC_EVENT_F_CLOSED;
	list_remove(&event->reactor_link);

	event->base.ref_count++;
	async_callbacks_notify((async_awaitable_t *) event, &event->base.callbacks, &event->result, NULL);
	async_io_event_release(event);
}

static zend_result reactor_submit(async_reactor_t *reactor, async_io_event_t *event, async_reactor_link_t *list)
{
#ifndef PHP_WIN32
	if (UNEXPECTED(reactor->queue != NULL && reactor->queue_pid != getpid())) {
		reactor_rebuild(reactor);

		if (UNEXPECTED(EG(exception) != NULL)) {
			return FAILURE;
		}
	}
#endif

	php_io_queue *queue = reactor_queue(reactor);

	if (UNEXPECTED(queue == NULL)) {
		return FAILURE;
	}

	if (UNEXPECTED(queue->ops->submit(queue, &event->op, event) == FAILURE)) {
		zend_throw_error(NULL, "Cannot submit an IO operation: %s", strerror(errno));
		return FAILURE;
	}

	list_add(list, &event->reactor_link);

	php_io_queue_completion completion;

	if (UNEXPECTED(queue->ops->take_inline(queue, &event->op, &completion))) {
		reactor_dispatch(&completion);
	}

	return SUCCESS;
}

zend_result async_io_event_submit(async_io_event_t *event)
{
	async_reactor_t *reactor = &ASYNC_G(reactor);

	return reactor_submit(reactor, event, &reactor->waits);
}

zend_result async_reactor_submit_own(async_io_event_t *event)
{
	async_reactor_t *reactor = &ASYNC_G(reactor);

	return reactor_submit(reactor, event, &reactor->own);
}

void async_io_event_orphan(async_io_event_t *event)
{
	php_io_queue *queue = event->op.queue;

	if (EXPECTED(queue != NULL)) {
		queue->ops->orphan(queue, &event->op);
	}

	list_remove(&event->reactor_link);
}

void async_io_record_unlink(async_coroutine_event_callback_t *record)
{
	async_io_event_t *event = (async_io_event_t *) record->event;

	async_wait_record_remove(record);

	if (EXPECTED(event->base.callbacks.length == 0 && !(event->base.flags & ASYNC_EVENT_F_CLOSED))) {
		async_io_event_orphan(event);
	}

	async_io_event_release(event);
}

///////////////////////////////////////////////////////////////////
/// Timers
///////////////////////////////////////////////////////////////////

static zend_string *timer_record_info(const async_coroutine_event_callback_t *record)
{
	(void) record;

	return zend_string_init("await: delay", sizeof("await: delay") - 1, false);
}

static zend_object *timer_result_error(const php_io_op_result *result)
{
	if (EXPECTED(result->status == PHP_IO_DONE && result->error == 0)) {
		return NULL;
	}

	return async_new_exception(
			zend_ce_error, "The timer ended with status %d: %s", (int) result->status, strerror(result->error));
}

static void
timer_record_wake(async_awaitable_t *target, async_event_callback_t *callback, void *result, zend_object *exception)
{
	(void) target;
	(void) exception;

	async_coroutine_t *waiter = ((async_coroutine_event_callback_t *) callback)->coroutine;
	async_scheduler_enqueue(&waiter->coroutine, timer_result_error(result), true);
}

/* TIMER (S4.md 3.5): the record owns the event's only reference, so the waiter's unlink frees it,
 * withdrawing the op first when the wake did not come from its fire (a cancel, a bailout). */
static const async_wait_kind_t timer_kind = {
	.info = timer_record_info,
	.unlink = async_io_record_unlink,
};

bool async_reactor_delay(async_coroutine_t *waiter, const zend_long ms)
{
	ZEND_ASSERT(ms > 0);

	/* A bailout that a shutdown function's zend_try caught can leave main's wait linked. */
	async_wait_end(waiter);

	php_deadline deadline = php_io_deadline_from_ms(ms);

	/* The core saturates a deadline past the clock's range to an infinite one, which the Ring refuses
	 * for a Timer (Rg:890-895): the latest finite one instead, as TrueAsync saturates the timer
	 * (libuv_reactor.c:1161-1178). */
	if (UNEXPECTED(php_deadline_is_infinite(&deadline))) {
		deadline.hrtime = ZEND_HRTIME_T_MAX - 1;
	}

	async_io_event_t *event = async_io_event_new();
	php_io_op_timer(&event->op, deadline);
	async_callbacks_reserve(&event->base.callbacks, 1);

	if (UNEXPECTED(async_io_event_submit(event) == FAILURE)) {
		async_io_event_release(event);
		return false;
	}

	/* The queue completed it at the submit, its deadline passed meanwhile: still a yield, as TrueAsync's
	 * delay() always parks, so the coroutines queued before it run first. */
	if (UNEXPECTED(event->base.flags & ASYNC_EVENT_F_CLOSED)) {
		zend_object *error = timer_result_error(&event->result);
		async_io_event_release(event);

		if (UNEXPECTED(error != NULL)) {
			zend_throw_exception_internal(error);
			return false;
		}

		if (UNEXPECTED(!async_scheduler_enqueue(&waiter->coroutine, NULL, false))) {
			return false;
		}

		return ZEND_ASYNC_SUSPEND();
	}

	async_wait_link(&waiter->waker.records[0], waiter, (async_awaitable_t *) event, &timer_kind, timer_record_wake);

	return ZEND_ASYNC_SUSPEND();
}

///////////////////////////////////////////////////////////////////
/// Polling
///////////////////////////////////////////////////////////////////

/* Dispatches completions, one per wait call (S4.md 3.2): a notify may withdraw another event's op,
 * whose completion would already sit in a batch. Blocks on the first call when `deadline` is
 * infinite. Stops at an exception a notify left. False only on EDEADLK; an error the scheduler
 * cannot go on with ends the request. */
static bool reactor_poll(async_reactor_t *reactor, php_deadline deadline)
{
	php_io_queue_completion completion;

	for (;;) {
		php_io_queue *queue = reactor->queue;
		const int count = queue->ops->wait(queue, &completion, 1, &deadline);

		if (EXPECTED(count == 0)) {
			return true;
		}

		if (UNEXPECTED(count < 0)) {
			switch (errno) {
				case EINTR:
					return true;
				case EDEADLK:
					return false;
#ifndef PHP_WIN32
				case EPERM:
					/* The parent's queue in a forked child; anything else is a hard error. */
					if (EXPECTED(reactor->queue_pid != getpid())) {
						reactor_rebuild(reactor);
						return true;
					}

					ZEND_FALLTHROUGH;
#endif
				default:
					/* D16's Timer rides this queue and does not fire here: each tick cancels every
					 * coroutine again instead (the Sage). */
					async_scheduler_exit_with(
							async_new_exception(zend_ce_error, "The IO queue's wait failed: %s", strerror(errno)));
					return true;
			}
		}

		reactor_dispatch(&completion);

		if (UNEXPECTED(EG(exception) != NULL)) {
			return true;
		}

		php_deadline_init_nonblock(&deadline);
	}
}

/* The coarse monotonic clock of the tick's throttle: TrueAsync's (scheduler.c:1543-1554), and
 * FreeBSD's equivalent. Elsewhere the precise clock, so the context loop polls on every pass, as
 * TrueAsync's does there. */
static zend_always_inline uint64_t coarse_now(void)
{
#if (defined(__linux__) && defined(CLOCK_MONOTONIC_COARSE)) || defined(CLOCK_MONOTONIC_FAST)
	struct timespec now;
#ifdef CLOCK_MONOTONIC_FAST
	clock_gettime(CLOCK_MONOTONIC_FAST, &now);
#else
	clock_gettime(CLOCK_MONOTONIC_COARSE, &now);
#endif

	return (uint64_t) now.tv_sec * ZEND_NANO_IN_SEC + (uint64_t) now.tv_nsec;
#else
	return zend_hrtime();
#endif
}

void async_reactor_poll_due(async_reactor_t *reactor, const uint64_t interval)
{
	const uint64_t now = coarse_now();

	if (EXPECTED(now - reactor->last_poll <= interval)) {
		return;
	}

	reactor->last_poll = now;

	php_deadline deadline;
	php_deadline_init_nonblock(&deadline);

	reactor_poll(reactor, deadline);
}

bool async_reactor_wait_idle(void)
{
	async_reactor_t *reactor = &ASYNC_G(reactor);

	ZEND_ASSERT(async_reactor_has_waits(reactor));

	php_deadline deadline;
	php_deadline_init_infinite(&deadline);

	return reactor_poll(reactor, deadline);
}
