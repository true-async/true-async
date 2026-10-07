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
#ifndef TRUE_ASYNC_TIMEOUT_H
#define TRUE_ASYNC_TIMEOUT_H

#include "php.h"
#include "main/php_io_hooks.h"
#include "true_async_API.h"
#include "reactor.h"

/* The cancellation token of Async\timeout() (dev/plans/S5.md, section 6, "Design for S5.4"; D32): one
 * absolute deadline for every wait that uses it, a timer armed only while one of them is parked, and
 * the outcome kept once it fires or is cancelled. */

/* Set on every Timeout event and never on a future event: src/await.c reads only these two. */
#define ASYNC_TIMEOUT_F_TIMEOUT (1u << 30)
#define ASYNC_TIMEOUT_F_CANCELLED (1u << ASYNC_EVENT_F_TYPE_SHIFT)

#define ASYNC_AWAITABLE_IS_TIMEOUT(awaitable) \
	((((const async_awaitable_t *) (awaitable))->flags & (ASYNC_AWAITABLE_F_EVENT | ASYNC_TIMEOUT_F_TIMEOUT)) == \
	 (ASYNC_AWAITABLE_F_EVENT | ASYNC_TIMEOUT_F_TIMEOUT))

typedef struct
{
	async_event_t base; /* CLOSED once fired or cancelled; ref_count: the object, the waits' holds and subscriptions */
	php_deadline deadline;
	zend_long ms;
	uint32_t subscriber_count;
	async_io_event_t *timer;               /* one reference while armed; NULL otherwise */
	async_event_callback_t timer_callback; /* in `timer`'s vector while armed */
	zend_object *exception;                /* the argument of cancel(), if any */
} async_timeout_event_t;

extern zend_class_entry *async_ce_timeout;

void async_register_timeout_ce(zend_class_entry *completable_interface);

void async_timeout_release(async_timeout_event_t *timeout);

/* Fires a Timeout whose deadline is not later than the clock; true once it has completed. */
bool async_timeout_fire_if_due(async_timeout_event_t *timeout);

/* The previous of the OperationCanceledException a wait the completed Timeout ends gets, with a
 * reference for the caller: a new TimeoutException for the deadline, the argument of cancel(), or
 * NULL for a bare cancel(). */
zend_object *async_timeout_exception(const async_timeout_event_t *timeout);

/* Counts a holder that waits for the Timeout, with a reference to the event, and arms the timer at 0
 * to 1. False when the Timeout has completed, the submit's own fire included, and false with an Error
 * when the submit failed: nothing is counted then. It allocates and may throw, so a wait subscribes
 * after its reservations and right before its first link. */
bool async_timeout_subscribe(async_timeout_event_t *timeout);

/* Counts down, disarms at 1 to 0 and drops the subscription's reference. Allocates nothing, runs no
 * PHP code. */
void async_timeout_unsubscribe(async_timeout_event_t *timeout);

#endif /* TRUE_ASYNC_TIMEOUT_H */
