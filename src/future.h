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
#ifndef TRUE_ASYNC_FUTURE_H
#define TRUE_ASYNC_FUTURE_H

#include "php.h"
#include "true_async_API.h"

/* Futures within one thread (dev/plans/S5.md, sections 2 and 3). A FutureState and every Future
 * on it point at one refcounted future event, which holds the outcome. map(), catch() and finally()
 * make a child Future with its own event; the source event keeps its children in `chain` until it
 * completes, then hands them to a drain coroutine that calls the mappers. */

/* An outcome nobody may warn about: ignore() of either end. */
#define ASYNC_FUTURE_F_IGNORED (1u << ASYNC_EVENT_F_TYPE_SHIFT)

/* The child Future objects of a pending source, in registration order; one reference each. */
typedef struct
{
	zend_object **children;
	uint32_t length;
	uint32_t capacity;
} async_future_chain_t;

/* 96 B, the fork's zend_future_t is 184 B. `base.ref_count` counts the objects that point at it and
 * the drains and waiters that use it; the last release reports an unobserved outcome. */
typedef struct
{
	async_event_t base; /* CLOSED once completed */
	zval result;        /* UNDEF until completed with a value */
	zend_object *exception;
	uint32_t created_lineno;
	uint32_t completed_lineno;
	zend_string *created_filename;   /* NULL when created outside PHP code */
	zend_string *completed_filename; /* NULL until completed, or completed outside PHP code */
	async_future_chain_t chain;      /* empty once completed */
} async_future_event_t;

#if SIZEOF_SIZE_T == 8
typedef char async_future_event_size_check[sizeof(async_future_event_t) == 96 ? 1 : -1];
#endif

extern zend_class_entry *async_ce_future_state;
extern zend_class_entry *async_ce_future;

void async_register_future_ce(zend_class_entry *completable_interface);

/* Drops one reference to `future`; the last one reports an outcome nobody observed and frees it,
 * which may run PHP code. */
void async_future_event_release(async_future_event_t *future);

/* The event of a Future object; NULL for one never constructed (unserialize()). */
async_future_event_t *async_future_event_from_object(zend_object *object);

/* A Future object over a new pending event, which it holds; `event` is borrowed. */
zend_object *async_future_new_pending(async_future_event_t **event);

/* Completes a pending `future` with `result` or `exception` (borrowed): its waiters wake and its
 * children go to a drain coroutine. Once only: the caller checks CLOSED. */
void async_future_event_resolve(async_future_event_t *future, zval *result, zend_object *exception);

/* Parks the current coroutine until `future` completes and puts its outcome in `return_value`, or
 * throws it. Marks nothing observed: the caller marks the event first. False with an exception
 * when there is no coroutine to park, the wait is aborted (a cancellation of the waiter), or
 * `token`, NULL or a token the caller holds for the call, completes first (dev/plans/S5.md,
 * section 4). */
bool async_future_await(async_future_event_t *future, zval *return_value, async_awaitable_t *token);

/* For the collector of coroutines that can never wake (collector.h): what a FutureState or a Future
 * object owns, and a record's wait for `future` that took a reference to it in C. */
void async_future_collector_references(zend_object *object, async_collector_t *collector);
void async_future_collector_target(async_collector_t *collector, async_future_event_t *future);
/* `future` as one a source outside the walk will complete (S7.md 3.4). */
void async_future_collector_live(async_collector_t *collector, async_future_event_t *future);

#endif /* TRUE_ASYNC_FUTURE_H */
