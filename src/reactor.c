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

/* Moves every link of `from` to the tail of `head`, in order. */
static zend_always_inline void list_splice_tail(async_reactor_link_t *head, async_reactor_link_t *from)
{
	if (list_is_empty(from)) {
		return;
	}

	from->next->prev = head->prev;
	head->prev->next = from->next;
	from->prev->next = head;
	head->prev = from->prev;
	list_init(from);
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
/// The timer heap
///////////////////////////////////////////////////////////////////

/* The TIMER events never reach the queue (S4.md 3.5): a min-heap on their deadline, whose top the
 * queue's wait gets as its limit, as libuv keeps its timers. A kernel timeout per Timer op makes the
 * Ring fall behind a burst of delays. Equal deadlines fire in either order. */

static zend_always_inline zend_hrtime_t timer_deadline(const async_io_event_t *event)
{
	return event->op.deadline.hrtime;
}

static zend_always_inline void timers_place(async_reactor_t *reactor, async_io_event_t *event, const uint32_t slot)
{
	reactor->timers[slot] = event;
	event->timer_index = slot + 1;
}

static void timers_sift_up(async_reactor_t *reactor, uint32_t slot)
{
	async_io_event_t *event = reactor->timers[slot];

	while (slot > 0) {
		const uint32_t parent = (slot - 1) / 2;

		if (timer_deadline(reactor->timers[parent]) <= timer_deadline(event)) {
			break;
		}

		timers_place(reactor, reactor->timers[parent], slot);
		slot = parent;
	}

	timers_place(reactor, event, slot);
}

static void timers_sift_down(async_reactor_t *reactor, uint32_t slot)
{
	async_io_event_t *event = reactor->timers[slot];
	const uint32_t count = reactor->timers_count;

	for (;;) {
		uint32_t child = slot * 2 + 1;

		if (child >= count) {
			break;
		}

		if (child + 1 < count && timer_deadline(reactor->timers[child + 1]) < timer_deadline(reactor->timers[child])) {
			child++;
		}

		if (timer_deadline(event) <= timer_deadline(reactor->timers[child])) {
			break;
		}

		timers_place(reactor, reactor->timers[child], slot);
		slot = child;
	}

	timers_place(reactor, event, slot);
}

/* A push from a timer's notify cannot fire in the same run: its deadline is at least the run's clock
 * + 1, so a notify that arms an expired deadline again cannot spin the run. */
static void timers_push(async_reactor_t *reactor, async_io_event_t *event)
{
	if (UNEXPECTED(timer_deadline(event) <= reactor->timers_run_now)) {
		event->op.deadline.hrtime = reactor->timers_run_now + 1;
	}

	if (UNEXPECTED(reactor->timers_count == reactor->timers_capacity)) {
		reactor->timers_capacity = reactor->timers_capacity == 0 ? 16 : reactor->timers_capacity * 2;
		reactor->timers = safe_erealloc(reactor->timers, reactor->timers_capacity, sizeof(*reactor->timers), 0);
	}

	timers_place(reactor, event, reactor->timers_count++);
	timers_sift_up(reactor, reactor->timers_count - 1);
}

/* From any slot: the last timer takes it and moves down or up. */
static void timers_remove(async_reactor_t *reactor, async_io_event_t *event)
{
	const uint32_t slot = event->timer_index - 1;
	async_io_event_t *last = reactor->timers[--reactor->timers_count];

	event->timer_index = 0;

	if (slot == reactor->timers_count) {
		return;
	}

	timers_place(reactor, last, slot);
	timers_sift_down(reactor, slot);
	timers_sift_up(reactor, last->timer_index - 1);
}

/* Every slot leaves, unrun: a fork rebuild. */
static void timers_clear(async_reactor_t *reactor)
{
	for (uint32_t slot = 0; slot < reactor->timers_count; slot++) {
		reactor->timers[slot]->timer_index = 0;
	}

	reactor->timers_count = 0;
	/* A bailout in a timer's notify left the run's clock, and a shutdown function may fork. */
	reactor->timers_run_now = 0;
}

/* The limit for the queue's wait: the nearest timer's deadline when it is nearer. */
static zend_always_inline php_deadline timers_limit(const async_reactor_t *reactor, php_deadline deadline)
{
	if (reactor->timers_count != 0 && timer_deadline(reactor->timers[0]) < deadline.hrtime) {
		deadline.hrtime = timer_deadline(reactor->timers[0]);
	}

	return deadline;
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

	for (int i = 0; i < 2; i++) {
		ioctlsocket(sockets[i], FIONBIO, &nonblocking);
		/* proc_open() creates its child with handle inheritance on. */
		SetHandleInformation((HANDLE) sockets[i], HANDLE_FLAG_INHERIT, 0);
	}

	pair->read_fd = sockets[0];
	pair->write_fd = sockets[1];
#else
	int fds[2];

#ifdef HAVE_PIPE2
	/* Atomic: between pipe() and fcntl() another thread's proc_open() would inherit the pair. */
	if (UNEXPECTED(pipe2(fds, O_NONBLOCK | O_CLOEXEC) != 0)) {
		return FAILURE;
	}
#else
	if (UNEXPECTED(pipe(fds) != 0)) {
		return FAILURE;
	}

	for (int i = 0; i < 2; i++) {
		fcntl(fds[i], F_SETFL, fcntl(fds[i], F_GETFL) | O_NONBLOCK);
		fcntl(fds[i], F_SETFD, FD_CLOEXEC);
	}
#endif

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

	/* No hint: without ior this is always taken. */
	if (queue == NULL) {
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
	list_splice_tail(&reactor->triggers, &reactor->triggers_to_walk);

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
					true,
					false);

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
}

/* In a forked child (S4.md 3.1): the parent's queue goes, and its waits with it, unrun; their
 * coroutines end in the child's deadlock resolution, and those waiting for a trigger at once. The
 * destroy detaches every op still on the queue. The reactor's own ops go to a new queue at once,
 * oldest first (with none, the next submit creates it). One that cannot be submitted and the ones
 * after it stay on the list unsubmitted for the rest of the request, and D16 is unbounded in that
 * child (a retry would cost every submit a branch for a fork during the shutdown and a queue the
 * child cannot create). */
static void reactor_rebuild(async_reactor_t *reactor)
{
	async_reactor_link_t *head = &reactor->waits;

	while (!list_is_empty(head)) {
		list_remove(head->next);
	}

	reactor->queue->ops->destroy(reactor->queue);
	reactor->queue = NULL;
	async_io_provider_queue_destroyed();
	/* Before `own` goes back: D16 is pushed again with it. */
	timers_clear(reactor);

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
			zend_object *error = EG(exception);

			/* Not the error of the call that found the fork: the wakeup is made again by the next
			 * trigger creation or start, which throws to its own caller; D16 lives only in an exit's
			 * drain, whose exception takes the error. */
			GC_ADDREF(error);
			zend_clear_exception();

			if (event == reactor->wakeup) {
				OBJ_RELEASE(error);
			} else {
				async_exit_exception_add(error);
			}

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

#ifndef PHP_WIN32
	if (ASYNC_G(signals) != NULL) {
		async_signal_rebuild();
	}
#endif

	/* The walk of fires made before the fork: raised last, so the wakeup cannot complete inline in its
	 * resubmit and run trigger callbacks inside the call that found the fork; the next poll delivers
	 * it. */
	if (reactor->wakeup != NULL) {
		wake_pair_raise(&ASYNC_G(wake_pair));
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
	list_init(&reactor->triggers_to_walk);
	reactor->started_triggers = 0;
	reactor->wakeup = NULL;
	reactor->timers = NULL;
	reactor->timers_count = 0;
	reactor->timers_capacity = 0;
	reactor->timers_run_now = 0;
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
	 * list, which the next request starts again, and its stop does nothing. A bailout in a walk's
	 * notify left the rest on the walk's list. */
	head = &reactor->triggers;
	list_splice_tail(head, &reactor->triggers_to_walk);

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
	ZEND_ASSERT(reactor->timers_count == 0);

	if (reactor->timers != NULL) {
		efree(reactor->timers);
		reactor->timers = NULL;
		reactor->timers_capacity = 0;
	}

	/* A bailout in a timer's notify left the run's clock. */
	reactor->timers_run_now = 0;

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
	event->timer_index = 0;
	event->reactor_link.prev = NULL;
	event->reactor_link.next = NULL;

	return event;
}

void async_io_event_release(async_io_event_t *event)
{
	if (--event->base.ref_count > 0) {
		return;
	}

	ZEND_ASSERT(event->op.queue == NULL && event->timer_index == 0 && event->reactor_link.prev == NULL &&
				"a submitted op is withdrawn first");

	async_callbacks_free((async_awaitable_t *) event, &event->base.callbacks);
	efree(event);
}

/* A completion of the reactor's queue or of a heap timer, `result` written: the event fires once, in
 * scheduler context (the notify sets it), under a reference of its own, since the wake of its last
 * waiter releases the record's. */
static void event_complete(async_io_event_t *event)
{
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

static void reactor_dispatch(const php_io_queue_completion *completion)
{
	async_io_event_t *event = completion->data;

	event->result = completion->result;
	event_complete(event);
}

/* Completes the timers due at the run's clock, nearest first, as a queue completion would: a notify
 * that withdraws another due timer takes it off the heap. Stops at an exception a notify left, as
 * reactor_poll does; the rest run at the next poll. */
static void timers_run(async_reactor_t *reactor)
{
	if (reactor->timers_count == 0) {
		return;
	}

	reactor->timers_run_now = zend_hrtime();

	while (reactor->timers_count != 0 && timer_deadline(reactor->timers[0]) <= reactor->timers_run_now) {
		async_io_event_t *event = reactor->timers[0];

		timers_remove(reactor, event);
		memset(&event->result, 0, sizeof(event->result));
		event->result.status = PHP_IO_DONE;
		event_complete(event);

		if (UNEXPECTED(EG(exception) != NULL)) {
			break;
		}
	}

	reactor->timers_run_now = 0;
}

/* At every reactor entry: a forked child rebuilds first (S4.md 3.1). */
static zend_always_inline void reactor_check_fork(async_reactor_t *reactor)
{
#ifndef PHP_WIN32
	if (UNEXPECTED(reactor->queue != NULL && reactor->queue_pid != getpid())) {
		reactor_rebuild(reactor);
	}
#endif
}

/* The submit to an existing queue, a TIMER's to the heap instead, without the dispatch of an inline
 * completion, which it returns in `completion` (`completed`). 0, or the queue's errno. */
static int queue_push(async_reactor_t *reactor,
					  async_io_event_t *event,
					  async_reactor_link_t *list,
					  php_io_queue_completion *completion,
					  bool *completed)
{
	php_io_queue *queue = reactor->queue;

	if (event->op.type == PHP_IO_OP_TIMER) {
		timers_push(reactor, event);
		list_add(list, &event->reactor_link);
		*completed = false;

		return 0;
	}

	if (UNEXPECTED(queue->ops->submit(queue, &event->op, event) == FAILURE)) {
		ZEND_ASSERT(errno != 0 && "a queue's failed submit sets errno");
		return errno;
	}

	list_add(list, &event->reactor_link);
	*completed = queue->ops->take_inline(queue, &event->op, completion);

	return 0;
}

/* queue_push() with FAILURE and an Error. */
static zend_result queue_submit(async_reactor_t *reactor,
								async_io_event_t *event,
								async_reactor_link_t *list,
								php_io_queue_completion *completion,
								bool *completed)
{
	const int error = queue_push(reactor, event, list, completion, completed);

	if (UNEXPECTED(error != 0)) {
		zend_throw_error(NULL, "Cannot submit an IO operation: %s", strerror(error));
		return FAILURE;
	}

	return SUCCESS;
}

static zend_result reactor_submit(async_reactor_t *reactor, async_io_event_t *event, async_reactor_link_t *list)
{
	php_io_queue_completion completion;
	bool completed;

	reactor_check_fork(reactor);

	php_io_queue *queue = reactor_queue(reactor);

	if (UNEXPECTED(queue == NULL)) {
		zend_throw_error(NULL, "Cannot create the IO queue");
		return FAILURE;
	}

	if (UNEXPECTED(queue_submit(reactor, event, list, &completion, &completed) == FAILURE)) {
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
	bool completed;

	reactor_check_fork(reactor);

	if (UNEXPECTED(reactor_queue(reactor) == NULL)) {
		return ENOSYS;
	}

	const int error = queue_push(reactor, event, list, &completion, &completed);

	if (UNEXPECTED(error != 0)) {
		return error;
	}

	if (UNEXPECTED(completed)) {
		reactor_dispatch(&completion);
	}

	return 0;
}

void async_reactor_check_fork(void)
{
	reactor_check_fork(&ASYNC_G(reactor));
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

	if (event->timer_index != 0) {
		timers_remove(&ASYNC_G(reactor), event);
	} else if (EXPECTED(queue != NULL)) {
		queue->ops->orphan(queue, &event->op);
	}

	list_remove(&event->reactor_link);
}

void async_io_record_unlink(async_coroutine_event_callback_t *record)
{
	async_io_event_t *event = (async_io_event_t *) record->event;

	async_wait_record_remove(record);

	/* Usually the fire unlinks: the event is CLOSED then. */
	if (UNEXPECTED(event->base.callbacks.length == 0 && !(event->base.flags & ASYNC_EVENT_F_CLOSED))) {
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

php_deadline async_reactor_deadline_from_ms(const zend_long ms)
{
	ZEND_ASSERT(ms > 0);

	/* Not php_io_deadline_from_ms(): its timeval's tv_sec is 32-bit on Windows and wraps past 2^31 s. */
	const zend_hrtime_t ns_in_ms = ZEND_NANO_IN_SEC / 1000;
	const zend_hrtime_t ns =
			(zend_ulong) ms < ZEND_HRTIME_T_MAX / ns_in_ms ? (zend_hrtime_t) ms * ns_in_ms : ZEND_HRTIME_T_MAX;
	php_deadline deadline = php_io_deadline_from_ns(ns);

	/* The core saturates a deadline past the clock's range to an infinite one, which the queue's wait
	 * would take for a deadlock: the latest finite one instead, as TrueAsync saturates the timer
	 * (libuv_reactor.c:1161-1178). */
	if (UNEXPECTED(php_deadline_is_infinite(&deadline))) {
		deadline.hrtime = ZEND_HRTIME_T_MAX - 1;
	}

	return deadline;
}

bool async_reactor_delay(async_coroutine_t *waiter, const zend_long ms)
{
	ZEND_ASSERT(ms > 0);

	/* A bailout that a shutdown function's zend_try caught can leave main's wait linked. */
	async_wait_end(waiter);

	const php_deadline deadline = async_reactor_deadline_from_ms(ms);

	async_io_event_t *event = async_io_event_new();
	php_io_op_timer(&event->op, deadline);
	async_callbacks_reserve(&event->base.callbacks, 1);

	if (UNEXPECTED(async_io_event_submit(event) == FAILURE)) {
		async_io_event_release(event);
		return false;
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
	async_reactor_link_t *pending = &reactor->triggers_to_walk;

	list_splice_tail(pending, &reactor->triggers);

	while (!list_is_empty(pending)) {
		async_trigger_t *trigger = trigger_from_link(pending->next);

		list_remove(&trigger->reactor_link);
		list_add_tail(&reactor->triggers, &trigger->reactor_link);

		if (EXPECTED(!atomic_exchange(&trigger->fired, false))) {
			continue;
		}

		trigger->base.ref_count++;
		async_callbacks_notify((async_awaitable_t *) trigger, &trigger->base.callbacks, NULL, NULL);
		async_trigger_release(trigger);

		if (UNEXPECTED(EG(exception) != exception_at_entry)) {
			list_splice_tail(&reactor->triggers, pending);
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

		if (UNEXPECTED(queue_submit(reactor, event, &reactor->own, &completion, &completed) == FAILURE)) {
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

	/* Triggers listed with no wakeup: a child whose rebuild could not resubmit it, so a fire made
	 * before the fork waits unwalked; one walk at the next poll finds it, and costs nothing else. */
	if (UNEXPECTED(!list_is_empty(&reactor->triggers))) {
		wake_pair_raise(pair);
	}

	return true;
}

async_trigger_t *async_trigger_new(void)
{
	async_reactor_t *reactor = &ASYNC_G(reactor);

	reactor_check_fork(reactor);

	if (UNEXPECTED(!wakeup_arm(reactor))) {
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

	reactor_check_fork(reactor);

	if (UNEXPECTED(!wakeup_arm(reactor))) {
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

#ifndef PHP_WIN32
	if (UNEXPECTED(ASYNC_G(signals) != NULL)) {
		async_signal_reblock();
	}
#endif

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
				default:
					async_scheduler_exit_with(
							async_new_exception(zend_ce_error, "The IO queue's wait failed: %s", strerror(errno)));
					return true;
			}
		}

		/* The wakeup arms itself again in its completion, so a thread that fires without pause could keep
		 * the loop going and the timers waiting: it ends the loop, as libuv's poll takes one batch. */
		const bool is_wakeup = completion.data == reactor->wakeup;

		reactor_dispatch(&completion);

		if (UNEXPECTED(EG(exception) != NULL) || is_wakeup) {
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
	/* The Poll queue delivers its ready list before it checks for a fork (P:634-636). */
	reactor_check_fork(reactor);

	if (UNEXPECTED(reactor->queue == NULL)) {
		return;
	}

	php_deadline deadline;
	php_deadline_init_nonblock(&deadline);

	reactor_poll(reactor, deadline);

	if (EXPECTED(EG(exception) == NULL)) {
		timers_run(reactor);
	}
}

bool async_reactor_wait_idle(void)
{
	async_reactor_t *reactor = &ASYNC_G(reactor);

	ZEND_ASSERT(async_reactor_has_waits(reactor));
	reactor_check_fork(reactor);

	/* Started triggers in a child whose rebuild could not make a queue: nothing can wake them. */
	if (UNEXPECTED(reactor->queue == NULL)) {
		return false;
	}

	php_deadline deadline;
	php_deadline_init_infinite(&deadline);

	if (UNEXPECTED(!reactor_poll(reactor, timers_limit(reactor, deadline)))) {
		return false;
	}

	if (EXPECTED(EG(exception) == NULL)) {
		timers_run(reactor);
	}

	return true;
}
