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
 * without freeing anything, and counts a parked stack only once one of its targets is live, as Go's
 * leak profile does. A holder the walk does not know counts as outside, so a miss is possible and a
 * false finding is not. */

#include "php.h"
#include "coroutine.h"

/* What an automatic run does with the coroutines it finds: true_async.partial_deadlock. */
typedef enum
{
	ASYNC_PARTIAL_DEADLOCK_OFF = 0, /* no automatic run */
	ASYNC_PARTIAL_DEADLOCK_REPORT,  /* one E_WARNING per coroutine */
} async_partial_deadlock_t;

/* The largest true_async.partial_deadlock_interval, in ms (24.8 days): the backed-off interval in ns
 * stays within 64 bits. */
#define ASYNC_COLLECTOR_INTERVAL_MAX INT32_MAX

/* For a kind's collector_target: the record's target is `target`; `owned` when the record holds a
 * reference to it that its waiter's frame does not. */
void async_collector_report_target(async_collector_t *collector, zend_object *target, bool owned);

/* Runs the walk now and returns the coroutines that can never wake, in registry order, as an array
 * of borrowed pointers the caller frees with efree(); NULL when there is none. Runs no PHP code. */
async_coroutine_t **async_collector_find(uint32_t *count);

/* The idle point of the scheduler's loop, before it blocks in the reactor (S7.md section 5): runs the
 * walk once true_async.partial_deadlock_interval allows and applies true_async.partial_deadlock to
 * what it finds. True when PHP code ran (a warning's error handler), which may have queued
 * coroutines: the loop then takes another pass before it blocks. Scheduler context. */
bool async_collector_idle(void);

void async_collector_request_startup(void);

#ifdef TRUE_ASYNC_TEST_HOOKS
/* The oracle of the fuzz runs (S7.md section 11): `waiter` is woken because `target` finished. A
 * waiter the collector found and nobody cancelled since was not stuck: the run aborts, unless the
 * waiter or the target was handed out to PHP code, which may cancel through the registry. */
void async_collector_check_wake(async_coroutine_t *waiter, const async_coroutine_t *target);

/* The same oracle for a cancel: a coroutine the collector found is cancelled only through the
 * registry's walks (registry_cancel) or by the bailout; anything else held it. */
void async_collector_check_cancel(async_coroutine_t *coroutine);
#endif

#endif /* TRUE_ASYNC_COLLECTOR_H */
