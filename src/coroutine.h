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
#ifndef TRUE_ASYNC_COROUTINE_H
#define TRUE_ASYNC_COROUTINE_H

#include "php.h"
#include "true_async_API.h"

/* Defined elsewhere: the fiber context in scheduler.h, the switch handlers in true_async_API.h. */
typedef struct _async_fiber_context_s async_fiber_context_t;
typedef struct _async_coroutine_switch_handlers_vector_s async_coroutine_switch_handlers_vector_t;

/* The coroutine and its PHP object in one allocation (dev/plans/S3.md, section 3.1). */
struct _async_coroutine_s
{
	zend_coroutine_t coroutine; /* flags at offset 0: the awaitable type bit is 0 */
	/* The stack it runs on: taken before the first run, the scheduler's at its creation, main's a copy of
	 * the engine's context; NULL once the coroutine finishes. */
	async_fiber_context_t *fiber_context;
	async_callbacks_vector_t callbacks; /* waiters' records and finish handlers */
	async_waker_t waker;
	async_scope_t *scope; /* NULL out of a scope (scope.h) */
	uint32_t scope_index;
#ifdef TRUE_ASYNC_TEST_HOOKS
	uint32_t found_run; /* the oracle's: the last run that found it (collector.h); in scope_index's padding */
#endif
	zend_object *deferred_cancellation;                        /* the cancel that arrived inside protect() */
	async_coroutine_switch_handlers_vector_t *switch_handlers; /* lazy */
	HashTable *finally_handlers;                               /* lazy: the closures of Coroutine::finally() */
	/* The callable and arguments of spawn(), which coroutine.fcall points to: one allocation less per
	 * spawn than the core's separate block (O6, measured in dev/BENCHMARKS.md). Its cache owns a
	 * reference to its object and closure and a copy of a __call trampoline; the core's block of a
	 * Fiber's coroutine owns none of them. */
	zend_fcall_t spawn_fcall;
	zend_object std; /* last: the properties table runs past the end */
};

/* The sizes of dev/plans/S3.md 3.1 with the waker of dev/plans/S4.md 2.2, checked at compile time on
 * 64-bit targets: 488 B with the scope's index and the finally handlers (S9), allocated as 472 in the
 * 512 B bin. */
#if SIZEOF_SIZE_T == 8
typedef char async_coroutine_size_check[sizeof(async_coroutine_t) == 488 ? 1 : -1];
typedef char async_coroutine_std_offset_check[offsetof(async_coroutine_t, std) == 432 ? 1 : -1];
#endif

extern zend_class_entry *async_ce_coroutine;

static zend_always_inline async_coroutine_t *async_coroutine_from_object(zend_object *object)
{
	return (async_coroutine_t *) ((char *) object - offsetof(async_coroutine_t, std));
}

/* The vector of an awaitable's subscribers: a coroutine's or an event's, by the type bit. */
static zend_always_inline async_callbacks_vector_t *async_awaitable_callbacks(async_awaitable_t *awaitable)
{
	return ASYNC_AWAITABLE_IS_COROUTINE(awaitable) ? &((async_coroutine_t *) awaitable)->callbacks
												   : &((async_event_t *) awaitable)->callbacks;
}

/* No record of the coroutine is linked and no block is left: the common case at a suspend()'s return
 * and at a finish. */
static zend_always_inline bool async_wait_is_empty(const async_coroutine_t *coroutine)
{
	const async_waker_t *waker = &coroutine->waker;

	return waker->records[0].event == NULL && waker->records[1].event == NULL && waker->block == NULL;
}

/* Unlinks every record of the coroutine's wait (the unlink sites, dev/plans/S3.md 4.4) and its
 * block's; keeps the block. Allocates nothing, runs no PHP code; nothing to do without a wait. */
static zend_always_inline void async_wait_unlink(async_coroutine_t *coroutine)
{
	if (EXPECTED(async_wait_is_empty(coroutine))) {
		return;
	}

	async_wait_unlink_linked(coroutine);
}

void async_register_coroutine_ce(zend_class_entry *completable_interface);

/* Runs the coroutine's body on the current context, then finishes it (async_coroutine_finalize)
 * and clears the current-coroutine slot. The coroutine is current and RUNNING. A bailout out of
 * the body sets ASYNC_COROUTINE_F_BAILOUT, finishes it and passes on. */
void async_coroutine_execute(async_coroutine_t *coroutine);

/* Finishes the coroutine: FINISHED, an exception pending in EG becomes its outcome, its waiters and
 * finish handlers run (is_bailout when ASYNC_COROUTINE_F_BAILOUT is set), an error nobody observed takes
 * its scope's route (S9-scope.md 4), its finally handlers start, it leaves its scope and the registry,
 * and the scheduler drops its birth reference. An outcome exception nobody can observe becomes the
 * request's exit exception (S3.md section 6). */
void async_coroutine_finalize(async_coroutine_t *coroutine);

/* The innermost user frame of a parked coroutine (S3.md section 2), or NULL: one that never ran,
 * runs or finished, or one the core parked with no PHP code on its stack (the GC's). */
zend_execute_data *async_coroutine_suspend_frame(async_coroutine_t *coroutine);

/* The request's exit exception (S3.md section 6): a later one takes the earlier as its previous.
 * Takes a reference. */
void async_exit_exception_add(zend_object *exception);
void async_unobserved_exception_add(zend_object *exception);

/* Runs `finally_handlers` (taken), each called with `target` (a reference taken; NULL passes null), in
 * workers of a new child scope of `scope` (a root when NULL), at the front of the queue, one more while a
 * handler waits, as TrueAsync's async_call_finally_handlers (coroutine.c:1275-1318). One handler's error is the
 * worker's outcome, several a CompositeException, and goes the worker's error route. False, with the handlers released,
 * when nothing can run them: the core turned async off, or the scheduler refused the worker. */
bool async_finally_handlers_start(HashTable *finally_handlers, async_scope_t *scope, zend_object *target);

#endif /* TRUE_ASYNC_COROUTINE_H */
