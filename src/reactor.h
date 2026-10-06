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
#ifndef TRUE_ASYNC_REACTOR_H
#define TRUE_ASYNC_REACTOR_H

/* The reactor (dev/plans/S4.md, section 3): one php_io_queue per thread and request, created by the
 * first submit, polled without blocking by the scheduler's tick and waited in when nothing is
 * runnable. Its completions wake coroutines through the wait-record layer. */

#include "php.h"
#include "main/php_io_hooks.h"
#include "true_async_API.h"

typedef struct _async_reactor_link_s async_reactor_link_t;

struct _async_reactor_link_s
{
	async_reactor_link_t *prev; /* NULL off the list */
	async_reactor_link_t *next;
};

/* One op the reactor submits and the waiters of its completion (S4.md 3.2). On the heap: a record
 * waiting for it owns a reference, so the op the queue points to outlives a frame a bailout
 * unwinds, until the record's unlink withdraws it. */
typedef struct _async_io_event_s async_io_event_t;

struct _async_io_event_s
{
	async_event_t base;      /* ref_count: the records and the event's other holders */
	php_io_op op;            /* built by the submitter; the queue owns it while it is submitted */
	php_io_op_result result; /* the completion's, written before the notify */
	/* Runs instead of the default completion (CLOSED, off the list, notify) with `result` written;
	 * NULL for the default. */
	void (*complete)(async_io_event_t *event);
	async_reactor_link_t reactor_link; /* on one of the reactor's lists while the op is submitted */
};

/* The thread's wake descriptors (S4.md 3.6): an eventfd, a pipe, or a loopback socket pair on
 * Windows. In the module globals, made at the thread's first trigger and closed at its GSHUTDOWN, so
 * a thread that still holds a trigger never writes a descriptor a request closed. */
typedef struct
{
	php_socket_t read_fd;  /* SOCK_ERR until made */
	php_socket_t write_fd; /* read_fd for an eventfd */
#ifndef PHP_WIN32
	pid_t pid; /* the process that made them: a forked child makes its own */
#endif
} async_wake_pair_t;

/* An event another thread fires (S4.md 3.6), TrueAsync's trigger event (libuv_reactor.c:4466-4584).
 * Owned by its thread; its holders keep it alive while other threads may fire it. */
typedef struct
{
	async_event_t base;                /* ref_count: its holders; never CLOSED */
	const async_wake_pair_t *pair;     /* the owner thread's, written by a fire on any thread */
	atomic_bool fired;                 /* set by a fire, cleared by the wakeup's walk before the notify */
	uint32_t start_count;              /* the TRIGGER records and the holders that started it */
	async_reactor_link_t reactor_link; /* on the reactor's `triggers` from its creation to its free */
} async_trigger_t;

/* The thread's reactor, in ASYNC_G(reactor). */
typedef struct
{
	php_io_queue *queue; /* NULL until the first submit */
#ifndef PHP_WIN32
	pid_t queue_pid; /* the process that created `queue`: a forked child rebuilds at its first submit */
#endif
	uint64_t last_poll; /* the coarse clock at the tick's last poll */
	/* The IO events whose op is submitted: nothing in the run queue ends their waits, so while the
	 * list has one, an idle scheduler waits instead of resolving a deadlock (S4.md 3.4). */
	async_reactor_link_t waits;
	/* The reactor's own ops (the wakeup, the D16 deadline): no coroutine waits for them, so they keep
	 * none from a deadlock; a fork rebuild submits them again on the child's queue. */
	async_reactor_link_t own;
	async_reactor_link_t triggers; /* every live trigger of the request, walked by the wakeup */
	uint32_t started_triggers;     /* the triggers started: each may wake a coroutine (S4.md 3.4) */
	async_io_event_t *wakeup;      /* the POLL op on the wake pair, armed from the first trigger on */
#ifdef TRUE_ASYNC_TEST_HOOKS
	bool test_poll_queue; /* TrueAsync\Test\reactor_use_poll_queue(): the Poll queue where the Ring exists */
#endif
} async_reactor_t;

void async_reactor_request_startup(void);

/* Withdraws what is still submitted and destroys the queue, after the scheduler unlinked every
 * wait. */
void async_reactor_request_shutdown(void);

/* A new IO event of `size` >= sizeof(async_io_event_t) bytes, with one reference, the caller's, and
 * an op to build; the bytes past the event are uninitialised. */
async_io_event_t *async_io_event_new_ex(size_t size);

static zend_always_inline async_io_event_t *async_io_event_new(void)
{
	return async_io_event_new_ex(sizeof(async_io_event_t));
}

void async_io_event_release(async_io_event_t *event);

/* Submits the event's op to the thread's queue, creating the queue on first use, and puts the event
 * on the waits list; an op the queue completed at once is dispatched before the return (the event is
 * CLOSED then). FAILURE with an Error. */
zend_result async_io_event_submit(async_io_event_t *event);

/* Submits one of the reactor's own ops, on the `own` list instead (S4.md 3.5): its owner holds the
 * event and withdraws it before the release. FAILURE with an Error. */
zend_result async_reactor_submit_own(async_io_event_t *event);

/* async_io_event_submit() for a caller that reports the error itself: 0, the submit's errno, ENOSYS
 * when no queue can be created, or -1 with the Error of a fork rebuild. */
int async_io_event_try_submit(async_io_event_t *event);

/* Withdraws a submitted op that has not completed: no completion comes for it. */
void async_io_event_orphan(async_io_event_t *event);

/* The unlink of a kind whose record waits for an IO event and owns a reference to it: the last
 * waiter to leave an event that has not fired withdraws its op. */
void async_io_record_unlink(async_coroutine_event_callback_t *record);

/* Closes the wake pair at the thread's end. */
void async_wake_pair_close(async_wake_pair_t *pair);

/* A trigger with one reference, the caller's, on the thread's wakeup; NULL with an Error. */
async_trigger_t *async_trigger_new(void);

void async_trigger_release(async_trigger_t *trigger);

/* Wakes the trigger's waiters at the owner thread's next poll; any thread, async-signal-safe.
 * Edge-triggered: fires before the next poll make one wake, and one that finds nobody waiting is
 * dropped, as TrueAsync's. */
void async_trigger_fire(async_trigger_t *trigger);

/* A holder that waits with a callback of its own counts the trigger for the deadlock between these
 * two, as TrueAsync's start() and stop(); a TRIGGER record does it itself. A stop of a trigger that
 * is not started does nothing. The start is false with an Error from a fork rebuild. */
bool async_trigger_start(async_trigger_t *trigger);
void async_trigger_stop(async_trigger_t *trigger);

/* Links `record` of the running `waiter` into the trigger: the TRIGGER kind. The caller reserved
 * room in the trigger's vector and suspends next. False with an Error, as the start. */
bool async_trigger_link(async_coroutine_event_callback_t *record, async_coroutine_t *waiter, async_trigger_t *trigger);

/* The thread's queue when this process created it; NULL when there is none, or in a forked child
 * before its rebuild, where the parent's queue must not be touched. Creates and rebuilds nothing. */
php_io_queue *async_reactor_live_queue(void);

/* Rebuilds the reactor in a forked child before its first submit, which does it otherwise: for a holder
 * that reads an IO event's place on the lists (a Timeout's timer). False with the rebuild's Error. */
bool async_reactor_check_fork(void);

/* Parks `waiter`, the running coroutine, on a Timer op for `ms` > 0 milliseconds: delay() (S4.md
 * 3.5). False with the exception that ended the wait (a cancellation). */
bool async_reactor_delay(async_coroutine_t *waiter, zend_long ms);

/* Whether a submitted op or another thread may still wake a coroutine (S4.md 3.4). */
static zend_always_inline bool async_reactor_has_waits(const async_reactor_t *reactor)
{
	return reactor->waits.next != &reactor->waits || reactor->started_triggers != 0;
}

/* The idle wait (S4.md 3.3), for a reactor that has waits: blocks until the queue completes
 * something, in scheduler context. False when the queue answers EDEADLK: nothing it holds can wake a
 * coroutine. */
bool async_reactor_wait_idle(void);

/* How long a coroutine that suspends with others queued goes without a poll at most, in ns of the
 * coarse clock: TrueAsync's REACTOR_CHECK_INTERVAL (php_async.h:56). */
#define ASYNC_REACTOR_CHECK_INTERVAL (100 * 1000 * 1000)

/* The tick's poll without blocking, for a reactor with a queue: once `interval` ns of the coarse
 * clock passed since the last one (S4.md 3.3); 0 polls once per clock tick, as TrueAsync's context
 * loop (scheduler.c:1880-1910). */
void async_reactor_poll_due(async_reactor_t *reactor, uint64_t interval);

#endif /* TRUE_ASYNC_REACTOR_H */
