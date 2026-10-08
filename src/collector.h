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
#ifndef TRUE_ASYNC_COLLECTOR_H
#define TRUE_ASYNC_COLLECTOR_H

/* The collector of coroutines that can never wake (dev/plans/S7.md): a parked coroutine none of whose
 * targets can be completed or cancelled by code that can still run, and whose own object no such code
 * holds. It finds them by PHP's trial deletion (zend_gc.c) over what the parked coroutines reach,
 * without freeing anything, and spreads liveness into a parked stack only once one of its targets is
 * live, as Go's leak profile does. A holder the walk does not know counts as outside, so a miss is possible and a
 * false finding is not. */

#include "php.h"
#include "coroutine.h"

/* What an automatic run does with the coroutines it finds: true_async.partial_deadlock. */
typedef enum
{
	ASYNC_PARTIAL_DEADLOCK_OFF = 0, /* no automatic run */
	ASYNC_PARTIAL_DEADLOCK_REPORT,  /* one E_WARNING per coroutine */
	ASYNC_PARTIAL_DEADLOCK_CANCEL,  /* the warning, then AsyncCancellation("Deadlock detected") into each but main */
} async_partial_deadlock_t;

/* The smallest true_async.partial_deadlock_interval but 0, in ms; 0 walks at every idle point, for
 * tests and fuzz. */
#define ASYNC_COLLECTOR_INTERVAL_MIN 1000

/* The largest true_async.partial_deadlock_interval, in ms (24.8 days): the backed-off interval in ns
 * stays within 64 bits. */
#define ASYNC_COLLECTOR_INTERVAL_MAX INT32_MAX

/* What an event of a type owns, reported with the functions below; NULL for a type that reports
 * nothing, whose contents then count as held from outside. */
typedef void (*async_collector_event_references_t)(async_event_t *event, async_collector_t *collector);

/* For a kind's collector_target: the record's target is `target`; `owned` when the wait took a
 * reference to it in C, released when the wait ends, which no slot the walk reads reports (a local of
 * an internal function, a block). */
void async_collector_report_target(async_collector_t *collector, zend_object *target, bool owned);
void async_collector_report_event_target(async_collector_t *collector,
										 async_event_t *target,
										 async_collector_event_references_t references,
										 bool owned);
/* For a kind's collector_target whose record waits for any of many sources (a scope's coroutines): the
 * record's target is the reach node found or added by `key`, which the caller makes live with
 * async_collector_report_reach_source() when `*added`. COLLECTOR_NONE and no `*added` outside the
 * wake-edge pass, which alone reads them. */
uint32_t async_collector_report_reach_target(async_collector_t *collector, const void *key, bool *added);
/* A live `source` makes the reach node `to` live; the source owns no reference to it. */
void async_collector_report_reach_source(async_collector_t *collector, zend_object *source, uint32_t to);
/* For a kind's collector_target whose record waits for an outside source this time (a Timeout): its
 * waiter is live. */
void async_collector_report_outside(async_collector_t *collector);

/* For a source the walk does not reach that will complete `event` (S7.md 3.4), before the wake edges:
 * the event and what it owns are live. */
void async_collector_report_live_event(async_collector_t *collector,
									   async_event_t *event,
									   async_collector_event_references_t references);

/* For a reporter of what a node owns: one reference each. */
void async_collector_report_object(async_collector_t *collector, zend_object *object);
void async_collector_report_zval(async_collector_t *collector, zval *value);
void async_collector_report_event(async_collector_t *collector,
								  async_event_t *event,
								  async_collector_event_references_t references);

/* A scope's reach (S7.md 10): a holder without a reference. A reach node, found or added by `key`, is
 * live once a node with an edge to it is, or once an object reported as its holder is held from
 * outside the walk; an edge makes `to` live with `from`. COLLECTOR_NONE (a failed run) passes
 * through. */
uint32_t async_collector_reach_node(async_collector_t *collector, const void *key, bool *added);
void async_collector_report_reach(async_collector_t *collector, uint32_t from, uint32_t to);
void async_collector_report_holder(async_collector_t *collector, zend_object *holder, uint32_t node);
/* A source the walk does not reach that may cancel through the reach node `node` (a scope's
 * disposeAfterTimeout() timer): the node is live. */
void async_collector_report_live_reach(async_collector_t *collector, uint32_t node);

/* Runs the walk now and returns the coroutines that can never wake, in registry order, as an array
 * of borrowed pointers the caller frees with efree(); NULL when there is none. Runs no PHP code. A
 * `ceiling` other than 0 stops a walk whose tables would take the memory in use past it, which then
 * finds nothing. */
async_coroutine_t **async_collector_find(uint32_t *count, size_t ceiling);

/* The idle point of the scheduler's loop, before it blocks in the reactor (S7.md section 5): runs the
 * walk once true_async.partial_deadlock_interval allows and applies true_async.partial_deadlock to
 * what it finds. True when it warned (an error handler may have run and queued coroutines) or
 * cancelled: the loop then takes another pass before it blocks. Scheduler context. */
bool async_collector_idle(void);

void async_collector_request_startup(void);

#ifdef TRUE_ASYNC_TEST_HOOKS
/* The oracle of the fuzz runs (S7.md section 11): `waiter` is woken because `target` finished. A
 * waiter the collector found and nobody cancelled since was not stuck: the run aborts, unless the
 * waiter or the target was handed out to PHP code, which may cancel through the registry. */
void async_collector_check_wake(async_coroutine_t *waiter, const async_coroutine_t *target);

/* The oracle for a wake by an event (a future, a token, a scope's cancel): `waiter` is woken because
 * the running code completed the event. Excused, and the waiter marked handed out: a completer in the
 * bailout, and a completer the waiter's last run found too, which runs only because something excused
 * woke it, such as the route's hand-out of a subtree (S9-scope.md 6). A handed-out completer is not:
 * get_coroutines() marks every coroutine, its running caller too. An outside source completes in
 * scheduler context, and what it holds the walk counts live. */
void async_collector_check_event_wake(async_coroutine_t *waiter);

/* The oracle at a notify site, before the notify, for the waiters whose records are in `callbacks`:
 * woken because `member` finished or became a zombie, or, with no member, because the running code
 * completed the event. The notify itself runs its callbacks in scheduler context, where the running
 * code is not known (S9-scope.md 6). */
void async_collector_check_records_wake(async_callbacks_vector_t *callbacks, const async_coroutine_t *member);

/* The same oracle for a cancel: a coroutine the collector found is cancelled only through the
 * registry's walks (registry_cancel), the `cancel` policy or the bailout; anything else held it. */
void async_collector_check_cancel(async_coroutine_t *coroutine);
#endif

#endif /* TRUE_ASYNC_COLLECTOR_H */
