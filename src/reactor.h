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
typedef struct
{
	async_event_t base;         /* ref_count: the records and the event's other holders */
	php_io_op op;               /* built by the submitter; the queue owns it while it is submitted */
	php_io_op_result result;    /* the completion's, written before the notify */
	async_reactor_link_t waits; /* on the reactor's list while the op is submitted */
} async_io_event_t;

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
#ifdef TRUE_ASYNC_TEST_HOOKS
	bool test_poll_queue; /* TrueAsync\Test\reactor_use_poll_queue(): the Poll queue where the Ring exists */
#endif
} async_reactor_t;

void async_reactor_request_startup(void);

/* Withdraws what is still submitted and destroys the queue, after the scheduler unlinked every
 * wait. */
void async_reactor_request_shutdown(void);

/* A new IO event with one reference, the caller's, and an op to build. */
async_io_event_t *async_io_event_new(void);

void async_io_event_release(async_io_event_t *event);

/* Submits the event's op to the thread's queue, creating the queue on first use, and puts the event
 * on the waits list; an op the queue completed at once is dispatched before the return (the event is
 * CLOSED then). FAILURE with an Error. */
zend_result async_io_event_submit(async_io_event_t *event);

/* Withdraws a submitted op that has not completed: no completion comes for it. */
void async_io_event_orphan(async_io_event_t *event);

/* The unlink of a kind whose record waits for an IO event and owns a reference to it: the last
 * waiter to leave an event that has not fired withdraws its op. */
void async_io_record_unlink(async_coroutine_event_callback_t *record);

/* Whether a submitted op may still wake a coroutine (S4.md 3.4). */
static zend_always_inline bool async_reactor_has_waits(const async_reactor_t *reactor)
{
	return reactor->waits.next != &reactor->waits;
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
