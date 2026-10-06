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
#include "io_provider.h"

#ifdef HAVE_IOR
#include "main/php_io_ring.h"
#endif

#if (defined(__linux__) && defined(CLOCK_MONOTONIC_COARSE)) || defined(CLOCK_MONOTONIC_FAST)
#include <time.h>
#endif

#ifdef PHP_WIN32
#include "win32/sockets.h"
#else
#include <fcntl.h>
#include <unistd.h>
#ifdef __linux__
#include <sys/eventfd.h>
#endif
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

static zend_always_inline void list_add_tail(async_reactor_link_t *head, async_reactor_link_t *link)
{
	list_add(head->prev, link);
}

static zend_always_inline async_io_event_t *io_event_from_link(async_reactor_link_t *link)
{
	return (async_io_event_t *) ((char *) link - offsetof(async_io_event_t, reactor_link));
}

static zend_always_inline async_trigger_t *trigger_from_link(async_reactor_link_t *link)
{
	return (async_trigger_t *) ((char *) link - offsetof(async_trigger_t, reactor_link));
}

///////////////////////////////////////////////////////////////////
/// The wake pair
///////////////////////////////////////////////////////////////////

/* The descriptors Io\Poll\NotifyHandle is built on (Ip:693-725), until the core exports them
 * (RFC-CHANGES.md 1). */

#ifdef PHP_WIN32
#define WAKE_PAIR_CLOSE(fd) closesocket(fd)
#else
#define WAKE_PAIR_CLOSE(fd) close(fd)
#endif

void async_wake_pair_close(async_wake_pair_t *pair)
{
	if (pair->read_fd == SOCK_ERR) {
		return;
	}

	if (pair->write_fd != pair->read_fd) {
		WAKE_PAIR_CLOSE(pair->write_fd);
	}

	WAKE_PAIR_CLOSE(pair->read_fd);
	pair->read_fd = SOCK_ERR;
	pair->write_fd = SOCK_ERR;
}

/* Makes the pair, closing the one there was (a forked child's copy of its parent's). FAILURE with
 * errno. */
static zend_result wake_pair_open(async_wake_pair_t *pair)
{
	async_wake_pair_close(pair);

#ifdef __linux__
	const int fd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);

	if (UNEXPECTED(fd < 0)) {
		return FAILURE;
	}

	pair->read_fd = fd;
	pair->write_fd = fd;
#elif defined(PHP_WIN32)
	SOCKET sockets[2];

	if (UNEXPECTED(socketpair(AF_INET, SOCK_STREAM, 0, sockets) != 0)) {
		return FAILURE;
	}

	u_long nonblocking = 1;
	ioctlsocket(sockets[0], FIONBIO, &nonblocking);
	ioctlsocket(sockets[1], FIONBIO, &nonblocking);
	pair->read_fd = sockets[0];
	pair->write_fd = sockets[1];
#else
	int fds[2];

	if (UNEXPECTED(pipe(fds) != 0)) {
		return FAILURE;
	}

	for (int i = 0; i < 2; i++) {
		fcntl(fds[i], F_SETFL, fcntl(fds[i], F_GETFL) | O_NONBLOCK);
		fcntl(fds[i], F_SETFD, FD_CLOEXEC);
	}

	pair->read_fd = fds[0];
	pair->write_fd = fds[1];
#endif

#ifndef PHP_WIN32
	pair->pid = getpid();
#endif

	return SUCCESS;
}

static zend_always_inline bool wake_pair_is_ours(const async_wake_pair_t *pair)
{
#ifndef PHP_WIN32
	return pair->read_fd != SOCK_ERR && pair->pid == getpid();
#else
	return pair->read_fd != SOCK_ERR;
#endif
}

/* Any thread, async-signal-safe. */
static void wake_pair_raise(const async_wake_pair_t *pair)
{
#ifdef __linux__
	const uint64_t one = 1;
	const ssize_t written = write(pair->write_fd, &one, sizeof(one));
#elif defined(PHP_WIN32)
	const char one = 1;
	const int written = send(pair->write_fd, &one, sizeof(one), 0);
#else
	const char one = 1;
	const ssize_t written = write(pair->write_fd, &one, sizeof(one));
#endif
	/* A full counter or pipe is readable already: nothing is lost. */
	(void) written;
}

static void wake_pair_drain(const async_wake_pair_t *pair)
{
#ifdef __linux__
	uint64_t count;
	const ssize_t got = read(pair->read_fd, &count, sizeof(count));
	(void) got;
#elif defined(PHP_WIN32)
	char buffer[64];

	while (recv(pair->read_fd, buffer, sizeof(buffer), 0) > 0) {
	}
#else
	char buffer[64];

	while (read(pair->read_fd, buffer, sizeof(buffer)) > 0) {
	}
#endif
}

///////////////////////////////////////////////////////////////////
/// The queue
///////////////////////////////////////////////////////////////////

/* The thread's queue, created on first use as TrueAsync starts libuv lazily
 * (libuv_reactor.c:343-349): the Ring where the core has ior, the Poll queue otherwise or when the
 * Ring cannot be created. NULL when neither can be created. */
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
		return NULL;
	}

	reactor->queue = queue;
#ifndef PHP_WIN32
	reactor->queue_pid = getpid();
#endif

	/* The second install trigger of the IO provider (dev/plans/S6.md section 2), and the point
	 * where its flags follow a new queue, a forked child's included. */
	async_io_provider_queue_created(queue);

	return queue;
}

static zend_result reactor_submit(async_reactor_t *reactor, async_io_event_t *event, async_reactor_link_t *list);

static zend_string *trigger_record_info(const async_coroutine_event_callback_t *record)
{
	(void) record;

	return zend_string_init("await: trigger", sizeof("await: trigger") - 1, false);
}

static void trigger_record_unlink(async_coroutine_event_callback_t *record)
{
	async_trigger_t *trigger = (async_trigger_t *) record->event;

	async_wait_record_remove(record);
	async_trigger_stop(trigger);
	async_trigger_release(trigger);
}

static void
trigger_record_wake(async_awaitable_t *target, async_event_callback_t *callback, void *result, zend_object *exception)
{
	(void) target;
	(void) result;
	(void) exception;

	async_coroutine_t *waiter = ((async_coroutine_event_callback_t *) callback)->coroutine;
	async_scheduler_enqueue(&waiter->coroutine, NULL, false);
}

/* TRIGGER (S4.md 3.6): the record starts the trigger and owns a reference to it. */
static const async_wait_kind_t trigger_kind = {
	.info = trigger_record_info,
	.unlink = trigger_record_unlink,
};

/* Every coroutine still linked to a trigger at a fork rebuild is the parent's: a child's waiter
 * starts its trigger, which rebuilds first. Each is cancelled at once, so a fire from a thread the
 * child starts does not resume a wait the parent started. */
static void triggers_end_parent_waits(async_reactor_t *reactor)
{
	reactor->started_triggers = 0;

	for (async_reactor_link_t *link = reactor->triggers.next; link != &reactor->triggers;) {
		async_trigger_t *trigger = trigger_from_link(link);
		async_callbacks_vector_t *vector = &trigger->base.callbacks;
		uint32_t i = 0;

		trigger->start_count = 0;
		trigger->base.ref_count++;

		/* The cancel's enqueue unlinks the wait, and a removal moves the last element into its slot. */
		while (i < vector->length) {
			async_event_callback_t *callback = async_callbacks_slots(vector)[i];

			if (!(callback->flags & ASYNC_CALLBACK_F_RECORD) || callback->kind != &trigger_kind) {
				i++;
				continue;
			}

			async_coroutine_event_callback_t *record = (async_coroutine_event_callback_t *) callback;
			async_coroutine_t *waiter = record->coroutine;

			waiter->coroutine.flags &= ~ASYNC_COROUTINE_F_PROTECTED;
			async_coroutine_cancel(
					waiter,
					async_new_exception(async_ce_cancellation,
										"The wait was started before fork() and cannot end in the child"),
					true);

			if (UNEXPECTED(record->event != NULL)) {
				i++;
			}
		}

		link = link->next;
		async_trigger_release(trigger);
	}
}

/* The child's wake pair, before its own ops are resubmitted: the wakeup polls it, and one write walks
 * a fire made before the rebuild. Without a pair the wakeup goes and the next trigger creation
 * throws. */
static void wakeup_rebuild(async_reactor_t *reactor)
{
	async_io_event_t *wakeup = reactor->wakeup;
	async_wake_pair_t *pair = &ASYNC_G(wake_pair);

	list_remove(&wakeup->reactor_link);

	if (UNEXPECTED(wake_pair_open(pair) == FAILURE)) {
		reactor->wakeup = NULL;
		async_io_event_release(wakeup);
		return;
	}

	php_io_op_poll(&wakeup->op, NULL, pair->read_fd, PHP_POLL_READ, php_io_deadline_infinite());
	list_add(&reactor->own, &wakeup->reactor_link);
	wake_pair_raise(pair);
}

/* In a forked child (S4.md 3.1): the parent's queue goes, and its waits with it, unrun; their
 * coroutines end in the child's deadlock resolution, and those waiting for a trigger at once. The
 * destroy detaches every op still on the queue. The reactor's own ops go to a new queue at once,
 * oldest first (with none, the next submit creates it). An Error when one cannot: it and the ones
 * after it stay on the list unsubmitted for the rest of the request, and D16 is unbounded in that
 * child (a retry would cost every submit a branch for a fork during the shutdown and a
 * queue the child cannot create). */
static void reactor_rebuild(async_reactor_t *reactor)
{
	async_reactor_link_t *head = &reactor->waits;

	while (!list_is_empty(head)) {
		list_remove(head->next);
	}

	reactor->queue->ops->destroy(reactor->queue);
	reactor->queue = NULL;
	async_io_provider_queue_destroyed();

	triggers_end_parent_waits(reactor);

	if (reactor->wakeup != NULL) {
		wakeup_rebuild(reactor);
	}

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
			break;
		}
	}

	/* An unsubmitted wakeup would let every trigger count with nothing to wake its waiters: it goes,
	 * and the next trigger creation or start makes it again or throws. */
	if (UNEXPECTED(reactor->wakeup != NULL && reactor->wakeup->op.queue == NULL)) {
		list_remove(&reactor->wakeup->reactor_link);
		async_io_event_release(reactor->wakeup);
		reactor->wakeup = NULL;
	}
}

void async_reactor_request_startup(void)
{
	async_reactor_t *reactor = &ASYNC_G(reactor);

	reactor->queue = NULL;
	reactor->last_poll = 0;
	list_init(&reactor->waits);
	list_init(&reactor->own);
	list_init(&reactor->triggers);
	reactor->started_triggers = 0;
	reactor->wakeup = NULL;
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

	/* A trigger may outlive the request (an object freed later in php_request_shutdown): it leaves the
	 * list, which the next request starts again, and its stop does nothing. */
	head = &reactor->triggers;

	while (!list_is_empty(head)) {
		trigger_from_link(head->next)->start_count = 0;
		list_remove(head->next);
	}

	reactor->started_triggers = 0;

	if (reactor->wakeup != NULL) {
		async_io_event_orphan(reactor->wakeup);
		async_io_event_release(reactor->wakeup);
		reactor->wakeup = NULL;
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

async_io_event_t *async_io_event_new_ex(size_t size)
{
	ZEND_ASSERT(size >= sizeof(async_io_event_t));

	async_io_event_t *event = emalloc(size);

	async_event_init(&event->base, 0);
	memset(&event->op, 0, sizeof(event->op));
	memset(&event->result, 0, sizeof(event->result));
	event->complete = NULL;
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

	if (event->complete != NULL) {
		event->complete(event);
		return;
	}

	event->base.flags |= ASYNC_EVENT_F_CLOSED;
	list_remove(&event->reactor_link);

	event->base.ref_count++;
	async_callbacks_notify((async_awaitable_t *) event, &event->base.callbacks, &event->result, NULL);
	async_io_event_release(event);
}

/* At every entry that may create or count something in the reactor: a forked child rebuilds first
 * (S4.md 3.1). False with an Error the rebuild left. */
static zend_always_inline bool reactor_check_fork(async_reactor_t *reactor)
{
#ifndef PHP_WIN32
	if (UNEXPECTED(reactor->queue != NULL && reactor->queue_pid != getpid())) {
		reactor_rebuild(reactor);

		if (UNEXPECTED(EG(exception) != NULL)) {
			return false;
		}
	}
#endif

	return true;
}

/* The submit to an existing queue, without the dispatch of an inline completion, which it returns in
 * `completion` (`completed`). FAILURE with an Error. */
static zend_result queue_submit(php_io_queue *queue,
								async_io_event_t *event,
								async_reactor_link_t *list,
								php_io_queue_completion *completion,
								bool *completed)
{
	if (UNEXPECTED(queue->ops->submit(queue, &event->op, event) == FAILURE)) {
		zend_throw_error(NULL, "Cannot submit an IO operation: %s", strerror(errno));
		return FAILURE;
	}

	list_add(list, &event->reactor_link);
	*completed = queue->ops->take_inline(queue, &event->op, completion);

	return SUCCESS;
}

static zend_result reactor_submit(async_reactor_t *reactor, async_io_event_t *event, async_reactor_link_t *list)
{
	php_io_queue_completion completion;
	bool completed;

	if (UNEXPECTED(!reactor_check_fork(reactor))) {
		return FAILURE;
	}

	php_io_queue *queue = reactor_queue(reactor);

	if (UNEXPECTED(queue == NULL)) {
		zend_throw_error(NULL, "Cannot create the IO queue");
		return FAILURE;
	}

	if (UNEXPECTED(queue_submit(queue, event, list, &completion, &completed) == FAILURE)) {
		return FAILURE;
	}

	if (UNEXPECTED(completed)) {
		reactor_dispatch(&completion);
	}

	return SUCCESS;
}

/* The body of async_io_event_try_submit(). */
static int reactor_try_submit(async_reactor_t *reactor, async_io_event_t *event, async_reactor_link_t *list)
{
	php_io_queue_completion completion;

	if (UNEXPECTED(!reactor_check_fork(reactor))) {
		return -1;
	}

	php_io_queue *queue = reactor_queue(reactor);

	if (UNEXPECTED(queue == NULL)) {
		return ENOSYS;
	}

	if (UNEXPECTED(queue->ops->submit(queue, &event->op, event) == FAILURE)) {
		ZEND_ASSERT(errno != 0 && "a queue's failed submit sets errno");
		return errno;
	}

	list_add(list, &event->reactor_link);

	if (UNEXPECTED(queue->ops->take_inline(queue, &event->op, &completion))) {
		reactor_dispatch(&completion);
	}

	return 0;
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

php_io_queue *async_reactor_live_queue(void)
{
	const async_reactor_t *reactor = &ASYNC_G(reactor);

#ifndef PHP_WIN32
	if (UNEXPECTED(reactor->queue != NULL && reactor->queue_pid != getpid())) {
		return NULL;
	}
#endif

	return reactor->queue;
}

int async_io_event_try_submit(async_io_event_t *event)
{
	async_reactor_t *reactor = &ASYNC_G(reactor);

	return reactor_try_submit(reactor, event, &reactor->waits);
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
/// Triggers
///////////////////////////////////////////////////////////////////

/* Notifies each trigger fired since the last walk (S4.md 3.6, point 5). The list moves to a local
 * head and each trigger goes back before its notify, as libuv's uv__async_io: a notify may free a
 * trigger or make one. An exception from a notify puts the rest back and writes the pair, so they
 * are walked at the next poll. */
static void triggers_walk(async_reactor_t *reactor, const async_wake_pair_t *pair)
{
	if (list_is_empty(&reactor->triggers)) {
		return;
	}

	const zend_object *exception_at_entry = EG(exception);
	async_reactor_link_t pending;

	pending.next = reactor->triggers.next;
	pending.prev = reactor->triggers.prev;
	pending.next->prev = &pending;
	pending.prev->next = &pending;
	list_init(&reactor->triggers);

	while (!list_is_empty(&pending)) {
		async_trigger_t *trigger = trigger_from_link(pending.next);

		list_remove(&trigger->reactor_link);
		list_add_tail(&reactor->triggers, &trigger->reactor_link);

		if (EXPECTED(!atomic_exchange(&trigger->fired, false))) {
			continue;
		}

		trigger->base.ref_count++;
		async_callbacks_notify((async_awaitable_t *) trigger, &trigger->base.callbacks, NULL, NULL);
		async_trigger_release(trigger);

		if (UNEXPECTED(EG(exception) != exception_at_entry)) {
			while (!list_is_empty(&pending)) {
				async_reactor_link_t *link = pending.next;

				list_remove(link);
				list_add_tail(&reactor->triggers, link);
			}

			wake_pair_raise(pair);
			return;
		}
	}
}

static bool wakeup_result_is_ready(const php_io_op_result *result)
{
	return result->status == PHP_IO_DONE && result->error == 0 && result->res > 0;
}

/* The wakeup's completion: drains the pair and polls it again before the walk, so a fire during the
 * walk raises a POLL that is armed. An inline completion (the pair raised again, or an arm that
 * failed) is taken here, not by a recursive dispatch. A POLL that ended without readiness or cannot
 * be submitted again ends the request, as a failed wait does: no trigger could wake anything. The
 * queue is the one that completed it, so no fork check runs here. */
static void wakeup_complete(async_io_event_t *event)
{
	async_reactor_t *reactor = &ASYNC_G(reactor);
	const async_wake_pair_t *pair = &ASYNC_G(wake_pair);
	php_io_queue_completion completion;
	bool completed;

	list_remove(&event->reactor_link);

	do {
		if (UNEXPECTED(!wakeup_result_is_ready(&event->result))) {
			const php_io_op_result result = event->result;

			reactor->wakeup = NULL;
			async_io_event_release(event);
			async_scheduler_exit_with(async_new_exception(zend_ce_error,
														  "The wakeup's poll ended with status %d: %s",
														  (int) result.status,
														  strerror(result.error)));
			return;
		}

		wake_pair_drain(pair);

		if (UNEXPECTED(queue_submit(reactor->queue, event, &reactor->own, &completion, &completed) == FAILURE)) {
			reactor->wakeup = NULL;
			async_io_event_release(event);
			return;
		}

		if (UNEXPECTED(completed)) {
			event->result = completion.result;
			list_remove(&event->reactor_link);
		}
	} while (UNEXPECTED(completed));

	triggers_walk(reactor, pair);
}

/* The wakeup POLL, armed by the request's first trigger and kept to RSHUTDOWN (M5). False with an
 * Error. */
static bool wakeup_arm(async_reactor_t *reactor)
{
	if (EXPECTED(reactor->wakeup != NULL)) {
		return true;
	}

	async_wake_pair_t *pair = &ASYNC_G(wake_pair);

	if (UNEXPECTED(!wake_pair_is_ours(pair) && wake_pair_open(pair) == FAILURE)) {
		char *reason = php_socket_strerror(php_socket_errno(), NULL, 0);
		zend_throw_error(NULL, "Cannot create the wake descriptors: %s", reason);
		efree(reason);
		return false;
	}

	async_io_event_t *event = async_io_event_new();

	event->complete = wakeup_complete;
	php_io_op_poll(&event->op, NULL, pair->read_fd, PHP_POLL_READ, php_io_deadline_infinite());
	reactor->wakeup = event;

	if (UNEXPECTED(reactor_submit(reactor, event, &reactor->own) == FAILURE)) {
		reactor->wakeup = NULL;
		async_io_event_release(event);
		return false;
	}

	if (UNEXPECTED(reactor->wakeup == NULL)) {
		if (EG(exception) == NULL) {
			zend_throw_error(NULL, "Cannot poll the wake descriptors");
		}

		return false;
	}

	return true;
}

async_trigger_t *async_trigger_new(void)
{
	async_reactor_t *reactor = &ASYNC_G(reactor);

	if (UNEXPECTED(!reactor_check_fork(reactor) || !wakeup_arm(reactor))) {
		return NULL;
	}

	async_trigger_t *trigger = emalloc(sizeof(async_trigger_t));

	async_event_init(&trigger->base, 0);
	trigger->pair = &ASYNC_G(wake_pair);
	atomic_init(&trigger->fired, false);
	trigger->start_count = 0;
	list_add_tail(&reactor->triggers, &trigger->reactor_link);

	return trigger;
}

void async_trigger_release(async_trigger_t *trigger)
{
	if (--trigger->base.ref_count > 0) {
		return;
	}

	/* A holder's dispose may stop it first. */
	async_callbacks_free((async_awaitable_t *) trigger, &trigger->base.callbacks);

	if (UNEXPECTED(trigger->start_count > 0)) {
		ASYNC_G(reactor).started_triggers--;
	}

	list_remove(&trigger->reactor_link);
	efree(trigger);
}

void async_trigger_fire(async_trigger_t *trigger)
{
	bool unfired = false;

	if (EXPECTED(atomic_compare_exchange_strong(&trigger->fired, &unfired, true))) {
		wake_pair_raise(trigger->pair);
	}
}

bool async_trigger_start(async_trigger_t *trigger)
{
	async_reactor_t *reactor = &ASYNC_G(reactor);

	if (UNEXPECTED(!reactor_check_fork(reactor) || !wakeup_arm(reactor))) {
		return false;
	}

	if (trigger->start_count++ == 0) {
		reactor->started_triggers++;
	}

	return true;
}

void async_trigger_stop(async_trigger_t *trigger)
{
	if (UNEXPECTED(trigger->start_count == 0)) {
		return;
	}

	if (--trigger->start_count == 0) {
		ASYNC_G(reactor).started_triggers--;
	}
}

bool async_trigger_link(async_coroutine_event_callback_t *record, async_coroutine_t *waiter, async_trigger_t *trigger)
{
	if (UNEXPECTED(!async_trigger_start(trigger))) {
		return false;
	}

	trigger->base.ref_count++;
	async_wait_link(record, waiter, (async_awaitable_t *) trigger, &trigger_kind, trigger_record_wake);

	return true;
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
					 * coroutine again instead. */
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

	/* Started triggers in a child whose rebuild could not make a queue: nothing can wake them. */
	if (UNEXPECTED(reactor->queue == NULL)) {
		return false;
	}

	php_deadline deadline;
	php_deadline_init_infinite(&deadline);

	return reactor_poll(reactor, deadline);
}
