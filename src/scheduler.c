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

/*
 * The scheduler behind the core's slots (dev/plans/S3.md, sections 5 and 7).
 *
 * Coroutines run in FIFO order from one run queue, on pooled fiber contexts, with no scheduler
 * coroutine: whoever gives up the CPU runs the tick (the microtasks) on its own stack, picks the next
 * coroutine and switches straight into it (section 4.2). A context outlives its coroutine. When the
 * coroutine finishes, the context's loop (fiber_entry) runs the next queued coroutine that has no
 * context of its own in place, without a switch; when the next one already has a context, it
 * switches there and parks in the pool, or ends when the pool is full.
 *
 * The main coroutine runs on the OS thread stack under a copy of the engine's context. When the
 * script ends, the core calls the suspend slot with from_main: main finishes, and the same stack
 * drains the queue (scheduler_drain), the place a context comes back to when nothing is left to
 * run. Then a new main coroutine is minted on that stack, so the code the core runs afterwards
 * (shutdown functions, destructors) still has a current coroutine.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "php.h"
#include "php_true_async.h"
#include "Zend/zend_observer.h"
#include "scheduler.h"
#include "coroutine.h"
#include "exceptions.h"
#include "internal/circular_buffer.h"

/* Contexts the pool keeps however short the run queue is (TrueAsync's policy, D23). */
#define ASYNC_FIBER_POOL_SIZE 4

/* The bottom frame of every coroutine's VM stack. Nameless: backtraces skip a frame without a
 * function name, as they skip the core's own fiber root frame. */
static zend_function root_function = { ZEND_INTERNAL_FUNCTION };

static ZEND_STACK_ALIGNED void fiber_entry(zend_fiber_transfer *transfer);

///////////////////////////////////////////////////////////////////
/// Fiber contexts and their pool
///////////////////////////////////////////////////////////////////

static void fiber_context_cleanup(zend_fiber_context *context)
{
	efree((async_fiber_context_t *) context);
}

/* A new context whose first entry runs fiber_entry; NULL with an exception when the stack cannot be
 * allocated (zend_fiber_init_context throws). */
static async_fiber_context_t *fiber_context_create(void)
{
	async_fiber_context_t *fiber_context = ecalloc(1, sizeof(async_fiber_context_t));
	const zend_result result =
			zend_fiber_init_context(&fiber_context->context, async_ce_coroutine, fiber_entry, EG(fiber_stack_size));

	if (UNEXPECTED(result == FAILURE)) {
		efree(fiber_context);
		return NULL;
	}

	fiber_context->context.cleanup = fiber_context_cleanup;

	return fiber_context;
}

/* A context for a coroutine that has none: a parked one from the pool, else a new one. */
static async_fiber_context_t *fiber_context_take(void)
{
	async_fiber_context_t *fiber_context = NULL;

	if (circular_buffer_pop_ptr(&ASYNC_G(fiber_context_pool), (void **) &fiber_context) == SUCCESS) {
		return fiber_context;
	}

	return fiber_context_create();
}

/* Parks a context that has nothing to run, or refuses when the pool has enough (D23): it keeps
 * ASYNC_FIBER_POOL_SIZE contexts, and more while the run queue is longer than the pool. */
static bool fiber_pool_keep(async_fiber_context_t *fiber_context)
{
	const size_t pooled = circular_buffer_count(&ASYNC_G(fiber_context_pool));

	if (pooled >= ASYNC_FIBER_POOL_SIZE && pooled >= circular_buffer_count(&ASYNC_G(run_queue))) {
		return false;
	}

	return circular_buffer_push_ptr_with_resize(&ASYNC_G(fiber_context_pool), fiber_context) == SUCCESS;
}

/* Ends every parked context: each wakes with no current coroutine, leaves its loop and switches
 * back here, where the switch destroys it. */
static void fiber_pool_teardown(void)
{
	async_fiber_context_t *fiber_context = NULL;

	ZEND_ASYNC_CURRENT_COROUTINE = NULL;

	while (circular_buffer_pop_ptr(&ASYNC_G(fiber_context_pool), (void **) &fiber_context) == SUCCESS) {
		zend_fiber_transfer transfer = { .context = &fiber_context->context, .flags = 0 };
		ZVAL_NULL(&transfer.value);

		zend_fiber_switch_context(&transfer);
	}
}

///////////////////////////////////////////////////////////////////
/// The run queue and switches
///////////////////////////////////////////////////////////////////

static zend_always_inline async_coroutine_t *run_queue_pop(void)
{
	async_coroutine_t *coroutine = NULL;

	if (EXPECTED(circular_buffer_pop_ptr(&ASYNC_G(run_queue), (void **) &coroutine) == SUCCESS)) {
		return coroutine;
	}

	return NULL;
}

/* The coroutine runs from here on: the current-coroutine slot is how its context learns whom it
 * runs. */
static zend_always_inline void make_current(async_coroutine_t *coroutine)
{
	ZEND_COROUTINE_SET_STATUS(&coroutine->coroutine, ZEND_COROUTINE_STATUS_RUNNING);
	ZEND_ASYNC_CURRENT_COROUTINE = &coroutine->coroutine;
}

/* Switches into `context`; returns the flags the context that switches back hands over (only
 * ZEND_FIBER_TRANSFER_FLAG_BAILOUT is used). */
static uint8_t switch_to(zend_fiber_context *context)
{
	zend_fiber_transfer transfer = { .context = context, .flags = 0 };
	ZVAL_NULL(&transfer.value);

	zend_fiber_switch_context(&transfer);

	ZEND_ASSERT(!(transfer.flags & ZEND_FIBER_TRANSFER_FLAG_ERROR) && "errors travel in the waker, not the transfer");

	return transfer.flags;
}

/* Where control goes when a context has nothing to run: the drain on the OS stack. While main lives
 * the queue is never empty here: a yielded main waits in it, and a main parked otherwise is woken by
 * the deadlock resolution (S3.8). */
static zend_fiber_context *scheduler_idle_context(void)
{
	ZEND_ASSERT(ZEND_ASYNC_MAIN_COROUTINE == NULL && "a context went idle while main was alive");

	return EG(main_fiber_context);
}

/* The scheduler's tick (section 4.2, step 3): the microtasks queued so far, in scheduler context, as
 * TrueAsync runs them on every pass of its loop. The first one that throws stops the tick, as in
 * TrueAsync, and its exception ends the request as the exit exception (section 6; the graceful
 * shutdown it starts is S3.8's); the rest wait for the next tick. */
static void scheduler_tick(void)
{
	circular_buffer_t *microtasks = &ASYNC_G(microtasks);
	zend_async_microtask_t *microtask = NULL;

	ZEND_ASYNC_IN_SCHEDULER_CONTEXT = true;

	while (circular_buffer_pop_ptr(microtasks, (void **) &microtask) == SUCCESS) {
		if (EXPECTED(!ZEND_ASYNC_MICROTASK_IS_CANCELLED(microtask))) {
			microtask->handler(microtask);
		}

		ZEND_ASYNC_MICROTASK_RELEASE(microtask);

		if (UNEXPECTED(EG(exception) != NULL)) {
			zend_object *exception = EG(exception);
			GC_ADDREF(exception);
			zend_clear_exception();
			async_exit_exception_add(exception);
			break;
		}
	}

	ZEND_ASYNC_IN_SCHEDULER_CONTEXT = false;
}

/* Runs coroutines on this context until it has nothing to run and the pool does not keep it.
 * Returns the context to switch to as this one ends: the next coroutine's, or the drain. */
static zend_fiber_context *fiber_loop(async_fiber_context_t *fiber_context)
{
	for (;;) {
		async_coroutine_t *coroutine = (async_coroutine_t *) ZEND_ASYNC_CURRENT_COROUTINE;

		/* The pool's teardown, on the OS stack, wakes a parked context with no current coroutine. */
		if (UNEXPECTED(coroutine == NULL)) {
			return scheduler_idle_context();
		}

		ZEND_ASSERT(coroutine->fiber_context == fiber_context);
		async_coroutine_execute(coroutine);
		scheduler_tick();

		async_coroutine_t *next = run_queue_pop();

		/* The in-place run: a coroutine that never ran takes this context, with no switch. */
		if (next != NULL && next->fiber_context == NULL) {
			next->fiber_context = fiber_context;
			make_current(next);
			continue;
		}

		zend_fiber_context *target = NULL;

		if (next != NULL) {
			target = &next->fiber_context->context;
			make_current(next);
		} else {
			target = scheduler_idle_context();
			ZEND_ASYNC_CURRENT_COROUTINE = NULL;
		}

		if (!fiber_pool_keep(fiber_context)) {
			return target;
		}

		const uint8_t flags = switch_to(target);
		ZEND_ASSERT(flags == 0 && "a parked context is woken only to run a coroutine or to end");
		(void) flags;
	}
}

/* The first entry into a context. A bailout out of a coroutine ends the context: it goes with the
 * bailout flag to the stack that owns the request, which re-raises it there (main.c catches it): a
 * parked main's, as in ts.c (section 4.2, U4), else the drain's. Nothing else runs in the catch:
 * zend_first_try left no bailout address behind it. */
static ZEND_STACK_ALIGNED void fiber_entry(zend_fiber_transfer *transfer)
{
	async_fiber_context_t *fiber_context = (async_fiber_context_t *) EG(current_fiber_context);
	zend_fiber_context *target = NULL;
	uint8_t flags = 0;

	ZEND_ASSERT(transfer->flags == 0 && "a context starts only to run a coroutine");

	/* The switcher's VM stack is saved with its state; a bailout before ours exists destroys none. */
	EG(vm_stack) = NULL;

	zend_first_try
	{
		/* The root frame has no caller: a coroutine's backtrace ends in it. */
		EG(current_execute_data) = NULL;
		zend_fiber_vm_stack_start(&fiber_context->context, &root_function);

		target = fiber_loop(fiber_context);
	}
	zend_catch
	{
		async_coroutine_t *main_coroutine = (async_coroutine_t *) ZEND_ASYNC_MAIN_COROUTINE;

		flags = ZEND_FIBER_TRANSFER_FLAG_BAILOUT;

		if (main_coroutine != NULL) {
			make_current(main_coroutine);
			target = &main_coroutine->fiber_context->context;
		} else {
			target = EG(main_fiber_context);
		}
	}
	zend_end_try();

	zend_vm_stack_destroy();

	/* The trampoline marks this context dead and switches to `target`, whose switch destroys it. */
	transfer->context = target;
	transfer->flags = flags;
	ZVAL_NULL(&transfer->value);
}

///////////////////////////////////////////////////////////////////
/// The main coroutine and the drain
///////////////////////////////////////////////////////////////////

/* Wraps the OS thread stack in a coroutine: a copy of the engine's context, so the coroutine owns a
 * switchable handle while the engine's original stays where control lands when main is gone. */
static async_coroutine_t *main_coroutine_adopt(void)
{
	async_coroutine_t *coroutine = async_coroutine_new();
	async_fiber_context_t *fiber_context = ecalloc(1, sizeof(async_fiber_context_t));
	zend_fiber_context *engine_context = EG(main_fiber_context);

	fiber_context->context = *engine_context;
	coroutine->fiber_context = fiber_context;
	EG(current_fiber_context) = &fiber_context->context;

	/* The observer sees a switch from the engine's context into main's, as for any coroutine. */
	fiber_context->context.status = ZEND_FIBER_STATUS_INIT;
	zend_observer_fiber_switch_notify(engine_context, &fiber_context->context);
	fiber_context->context.status = ZEND_FIBER_STATUS_RUNNING;

	/* A bailout out of a notify leaves the flag set (the notify has no try); main runs outside the
	 * scheduler context, as in ts.c. */
	ZEND_ASYNC_IN_SCHEDULER_CONTEXT = false;

	coroutine->coroutine.flags |= ZEND_COROUTINE_F_MAIN | ZEND_COROUTINE_F_STARTED;
	ZEND_COROUTINE_SET_STATUS(&coroutine->coroutine, ZEND_COROUTINE_STATUS_RUNNING);
	zend_hash_index_add_new_ptr(&ASYNC_G(coroutines), coroutine->std.handle, coroutine);

	return coroutine;
}

/* Main's end: its context copy goes, the OS stack is the engine's context again, and main finishes
 * like any coroutine (its waiters and finish handlers run). */
static void main_coroutine_finish(async_coroutine_t *coroutine, const bool is_bailout)
{
	EG(current_fiber_context) = EG(main_fiber_context);
	efree(coroutine->fiber_context);
	coroutine->fiber_context = NULL;
	ZEND_ASYNC_MAIN_COROUTINE = NULL;

	if (UNEXPECTED(is_bailout)) {
		coroutine->coroutine.flags |= ASYNC_COROUTINE_F_BAILOUT;
	}

	async_coroutine_finalize(coroutine);
	ZEND_ASYNC_CURRENT_COROUTINE = NULL;
}

/* Runs the queue empty from the OS stack. A context comes back here when it has nothing left to
 * run; true when one came back with a bailout. */
static bool scheduler_drain(void)
{
	async_coroutine_t *coroutine = NULL;

	for (;;) {
		scheduler_tick();
		coroutine = run_queue_pop();

		if (coroutine == NULL) {
			break;
		}

		/* Current until it starts: a bailout before its body runs (no stack, no VM stack) leaves it
		 * to the bailout's drop. */
		make_current(coroutine);

		if (coroutine->fiber_context == NULL) {
			coroutine->fiber_context = fiber_context_take();

			/* No stack for it, and a user exception handler took the exception (without one, the
			 * exception has no frame here and is fatal): it finishes unrun (section 6). */
			if (UNEXPECTED(coroutine->fiber_context == NULL)) {
				ZEND_ASYNC_CURRENT_COROUTINE = NULL;
				async_coroutine_finalize(coroutine);
				continue;
			}
		}

		if (UNEXPECTED(switch_to(&coroutine->fiber_context->context) & ZEND_FIBER_TRANSFER_FLAG_BAILOUT)) {
			return true;
		}
	}

	return false;
}

/* After a bailout nothing more runs: a coroutine that never started, the drain's current one
 * included, finishes with is_bailout handlers. A started one that yielded is still queued until
 * S3.10 unwinds it (4.5); the assert stops there. A yielded main keeps out of it: it finishes in
 * the from_main call that drops the queue. */
static void scheduler_drop_queue(void)
{
	async_coroutine_t *coroutine = (async_coroutine_t *) ZEND_ASYNC_CURRENT_COROUTINE;

	if (coroutine != NULL && !ZEND_COROUTINE_IS_STARTED(&coroutine->coroutine)) {
		ZEND_ASYNC_CURRENT_COROUTINE = NULL;
		coroutine->coroutine.flags |= ASYNC_COROUTINE_F_BAILOUT;
		async_coroutine_finalize(coroutine);
	}

	while ((coroutine = run_queue_pop()) != NULL) {
		if (&coroutine->coroutine == ZEND_ASYNC_MAIN_COROUTINE) {
			continue;
		}

		ZEND_ASSERT(!ZEND_COROUTINE_IS_STARTED(&coroutine->coroutine));
		coroutine->coroutine.flags |= ASYNC_COROUTINE_F_BAILOUT;
		async_coroutine_finalize(coroutine);
	}
}

/* The suspend slot's from_main calls (section 7): main finishes, the queue drains, a new main is
 * minted on the same stack. A bailout, the call's or the drain's, is re-raised on the way out after
 * the new main exists, so whatever runs after the core's catch has a current coroutine. */
static bool scheduler_main_suspend(const bool is_bailout)
{
	async_coroutine_t *main_coroutine = (async_coroutine_t *) ZEND_ASYNC_MAIN_COROUTINE;
	bool bailout = is_bailout;

	/* A bailout out of the previous call's drain ends main before this call. */
	if (EXPECTED(main_coroutine != NULL)) {
		/* While main is still main: the drop skips the entry of a main that yielded, which must not
		 * outlive it. */
		if (UNEXPECTED(is_bailout)) {
			scheduler_drop_queue();
		}

		main_coroutine_finish(main_coroutine, is_bailout);
	}

	if (EXPECTED(!bailout)) {
		bailout = scheduler_drain();
	}

	if (UNEXPECTED(bailout)) {
		scheduler_drop_queue();
	}

	fiber_pool_teardown();

	main_coroutine = main_coroutine_adopt();
	ZEND_ASYNC_MAIN_COROUTINE = &main_coroutine->coroutine;
	ZEND_ASYNC_CURRENT_COROUTINE = &main_coroutine->coroutine;

	/* The bailout's own error is what the request reports; the exit exception is dropped, as in
	 * TrueAsync. */
	if (UNEXPECTED(bailout)) {
		if (UNEXPECTED(ZEND_ASYNC_EXIT_EXCEPTION != NULL)) {
			OBJ_RELEASE(ZEND_ASYNC_EXIT_EXCEPTION);
			ZEND_ASYNC_EXIT_EXCEPTION = NULL;
		}

		zend_bailout();
	}

	/* main.c prints only EG(exception): the exit exception goes there. */
	if (UNEXPECTED(ZEND_ASYNC_EXIT_EXCEPTION != NULL)) {
		zend_object *exit_exception = ZEND_ASYNC_EXIT_EXCEPTION;
		ZEND_ASYNC_EXIT_EXCEPTION = NULL;

		if (UNEXPECTED(EG(exception) != NULL)) {
			zend_exception_set_previous(EG(exception), exit_exception);
		} else {
			EG(exception) = exit_exception;
		}
	}

	return EG(exception) == NULL;
}

///////////////////////////////////////////////////////////////////
/// The slots
///////////////////////////////////////////////////////////////////

async_coroutine_t *async_coroutine_new(void)
{
	return async_coroutine_from_object(async_ce_coroutine->create_object(async_ce_coroutine));
}

static zend_coroutine_t *scheduler_new_coroutine(size_t extra_size)
{
	(void) extra_size;

	return &async_coroutine_new()->coroutine;
}

/* GC coroutines run in the same FIFO order as every other (D20). */
static zend_coroutine_t *scheduler_gc_new_coroutine(void)
{
	return &async_coroutine_new()->coroutine;
}

static zend_coroutine_t *scheduler_launch(void)
{
	return &main_coroutine_adopt()->coroutine;
}

/* The waker keeps one error until the switch-in: a cancellation replaces a plain error and keeps
 * an earlier cancellation (section 6). Takes a reference to `error`. */
static void waker_apply_error(async_coroutine_t *coroutine, zend_object *error)
{
	zend_object *current = coroutine->waker.error;

	if (current != NULL &&
		(instanceof_function(current->ce, async_ce_cancellation) ||
		 !instanceof_function(error->ce, async_ce_cancellation))) {
		OBJ_RELEASE(error);
		return;
	}

	if (current != NULL) {
		OBJ_RELEASE(current);
	}

	coroutine->waker.error = error;
}

/* Removes the records of the coroutine's wait from their targets (section 4.4). No wait links a
 * record before S3.7. */
static zend_always_inline void async_wait_unlink(async_coroutine_t *coroutine)
{
	ZEND_ASSERT(coroutine->waker.wait == NULL);
	(void) coroutine;
}

static zend_always_inline void run_queue_push(async_coroutine_t *coroutine)
{
	/* The front once after asHiPriority() (D20, D35). */
	if (UNEXPECTED(coroutine->coroutine.flags & ASYNC_COROUTINE_F_HI_PRIORITY)) {
		coroutine->coroutine.flags &= ~ASYNC_COROUTINE_F_HI_PRIORITY;
		circular_buffer_push_front(&ASYNC_G(run_queue), &coroutine, true);
	} else {
		circular_buffer_push_ptr_with_resize(&ASYNC_G(run_queue), coroutine);
	}

	ZEND_COROUTINE_SET_STATUS(&coroutine->coroutine, ZEND_COROUTINE_STATUS_QUEUED);
}

bool async_scheduler_enqueue(zend_coroutine_t *zend_coroutine, zend_object *error, const bool transfer_error)
{
	async_coroutine_t *coroutine = (async_coroutine_t *) zend_coroutine;

	if (UNEXPECTED(ZEND_COROUTINE_IS_FINISHED(zend_coroutine))) {
		if (error != NULL && transfer_error) {
			OBJ_RELEASE(error);
		}

		zend_throw_error(NULL, "Cannot enqueue a finished coroutine");
		return false;
	}

	if (UNEXPECTED(error != NULL)) {
		if (!transfer_error) {
			GC_ADDREF(error);
		}

		waker_apply_error(coroutine, error);
	}

	/* The current coroutine woken inside its own tick (U2): it is SUSPENDED or QUEUED there, and a
	 * push by the status would switch into the running context later. */
	if (UNEXPECTED(zend_coroutine == ZEND_ASYNC_CURRENT_COROUTINE && ZEND_ASYNC_IN_SCHEDULER_CONTEXT)) {
		async_wait_unlink(coroutine);

		if (ZEND_COROUTINE_IS_SUSPENDED(zend_coroutine)) {
			ZEND_COROUTINE_SET_STATUS(zend_coroutine, ZEND_COROUTINE_STATUS_RUNNING);
		}

		return true;
	}

	switch (ZEND_COROUTINE_STATUS(zend_coroutine)) {
		case ZEND_COROUTINE_STATUS_CREATED:
			/* The registry holds every enqueued coroutine until it finishes; the scope hook of
			 * S9 goes here. */
			zend_hash_index_add_new_ptr(&ASYNC_G(coroutines), coroutine->std.handle, coroutine);
			run_queue_push(coroutine);
			return true;
		case ZEND_COROUTINE_STATUS_SUSPENDED:
			run_queue_push(coroutine);
			async_wait_unlink(coroutine);
			return true;
		case ZEND_COROUTINE_STATUS_QUEUED:
			return true;
		case ZEND_COROUTINE_STATUS_RUNNING:
			/* The yield of Async\suspend(): the current coroutine goes to the back of the queue. */
			if (zend_coroutine == ZEND_ASYNC_CURRENT_COROUTINE) {
				run_queue_push(coroutine);
				return true;
			}

			zend_throw_error(NULL, "Cannot resume a coroutine that has not been suspended");
			return false;
		default:
			ZEND_UNREACHABLE();
			return false;
	}
}

/* Parks the current coroutine (section 4.2). The tick runs on its stack, then it switches straight
 * to the next queued coroutine and returns when somebody switches back. A yield keeps the coroutine
 * QUEUED, anything else parks it SUSPENDED; woken in the tick, or first in the queue after its own
 * yield, it runs on with no switch. */
static bool scheduler_suspend(const bool from_main, const bool is_bailout)
{
	if (from_main) {
		return scheduler_main_suspend(is_bailout);
	}

	zend_coroutine_t *zend_coroutine = ZEND_ASYNC_CURRENT_COROUTINE;
	async_coroutine_t *coroutine = (async_coroutine_t *) zend_coroutine;

	if (UNEXPECTED(zend_coroutine == NULL)) {
		zend_throw_error(NULL, "There is no coroutine to suspend");
		return false;
	}

	/* A park from the tick would leave the tick halfway (4.6); D14 for a blocked switch. */
	if (UNEXPECTED(ZEND_ASYNC_IN_SCHEDULER_CONTEXT)) {
		async_wait_unlink(coroutine);
		zend_throw_error(NULL, "A coroutine cannot be stopped from the Scheduler context");
		return false;
	}

	/* Inside a Fiber the scheduler did not adopt (until S3.9) the stack is the Fiber's: parking it as
	 * this coroutine would resume the coroutine inside the Fiber later. */
	if (UNEXPECTED(zend_fiber_switch_blocked() || EG(current_fiber_context) != &coroutine->fiber_context->context)) {
		async_wait_unlink(coroutine);
		zend_throw_error(NULL, "Cannot switch coroutines in the current execution context");
		return false;
	}

	/* The switch does not carry EG(exception), and the next coroutine's call would return at once
	 * with it set. */
	zend_object *saved_exception = NULL;
	async_exception_save_fast(&EG(exception), &saved_exception);

	/* getTrace(), the suspend location and the GC read the parked frame from here. */
	coroutine->fiber_context->execute_data = EG(current_execute_data);

	if (!ZEND_COROUTINE_IS_QUEUED(zend_coroutine)) {
		ZEND_COROUTINE_SET_STATUS(zend_coroutine, ZEND_COROUTINE_STATUS_SUSPENDED);
	}

	scheduler_tick();

	/* Whoever switches back here has made this coroutine current and RUNNING. */
	while (!ZEND_COROUTINE_IS_RUNNING(zend_coroutine)) {
		async_coroutine_t *next = run_queue_pop();

		/* The deadlock resolution (S3.8) wakes every parked coroutine before this point. */
		ZEND_ASSERT(next != NULL && "a suspend found nobody to run");

		/* A yield with nobody ahead (B3). */
		if (next == coroutine) {
			ZEND_COROUTINE_SET_STATUS(zend_coroutine, ZEND_COROUTINE_STATUS_RUNNING);
			break;
		}

		if (next->fiber_context == NULL) {
			next->fiber_context = fiber_context_take();

			/* No stack for it: it finishes unrun with that exception as its outcome, as in the drain. */
			if (UNEXPECTED(next->fiber_context == NULL)) {
				async_coroutine_finalize(next);
				continue;
			}
		}

		make_current(next);

		/* Only a parked main gets a bailout here (fiber_entry); it is re-raised on main's stack (U4). */
		if (UNEXPECTED(switch_to(&next->fiber_context->context) & ZEND_FIBER_TRANSFER_FLAG_BAILOUT)) {
			async_wait_unlink(coroutine);
			zend_bailout();
		}
	}

	async_wait_unlink(coroutine);

	/* Woken with an error (a cancellation, S3.8): it is thrown here, and a result that came with it
	 * is dropped. */
	if (UNEXPECTED(coroutine->waker.error != NULL)) {
		zend_object *error = coroutine->waker.error;
		coroutine->waker.error = NULL;
		zval_ptr_dtor(&coroutine->waker.result);
		ZVAL_UNDEF(&coroutine->waker.result);
		zend_throw_exception_internal(error);
	}

	async_exception_restore_fast(&EG(exception), &saved_exception);

	return EG(exception) == NULL;
}

/* The slots of later steps refuse the way their contract allows: with an exception where it
 * names one, with "nothing done" otherwise. */
static bool scheduler_cancel(zend_coroutine_t *coroutine, zend_object *error, bool transfer_error, const bool is_safely)
{
	(void) coroutine;
	(void) is_safely;

	if (error != NULL && transfer_error) {
		OBJ_RELEASE(error);
	}

	zend_throw_error(NULL, "Coroutine cancellation is not implemented yet");
	return false;
}

static bool scheduler_shutdown(void)
{
	return true;
}

/* The queue takes the caller's reference; the tick releases it. */
static bool scheduler_defer(zend_async_microtask_t *microtask)
{
	return circular_buffer_push_ptr_with_resize(&ASYNC_G(microtasks), microtask) == SUCCESS;
}

/* NULL keeps a starting fiber on the engine's own path until S3.9 adopts it. */
static zend_coroutine_t *scheduler_intercept_fiber(zend_fiber *fiber)
{
	(void) fiber;

	return NULL;
}

/* A wait not possible here: false without an exception (the GC then collects later). */
static bool scheduler_await(zend_coroutine_t *coroutine)
{
	(void) coroutine;

	return false;
}

static uint32_t scheduler_add_switch_handler(zend_coroutine_t *coroutine, zend_coroutine_switch_handler_fn handler)
{
	(void) coroutine;
	(void) handler;

	return 0;
}

static bool scheduler_remove_switch_handler(zend_coroutine_t *coroutine, uint32_t handler_id)
{
	(void) coroutine;
	(void) handler_id;

	return false;
}

static uint32_t
scheduler_add_awaiting_info(zend_coroutine_t *coroutine, zend_coroutine_awaiting_info_fn handler, void *data)
{
	(void) coroutine;
	(void) handler;
	(void) data;

	return 0;
}

static bool scheduler_remove_awaiting_info(zend_coroutine_t *coroutine, uint32_t handler_id)
{
	(void) coroutine;
	(void) handler_id;

	return false;
}

static zend_array *scheduler_get_awaiting_info(zend_coroutine_t *coroutine)
{
	(void) coroutine;

	return NULL;
}

static zend_class_entry *scheduler_get_class_ce(const zend_async_class type)
{
	switch (type) {
		case ZEND_ASYNC_CLASS_COROUTINE:
			return async_ce_coroutine;
		case ZEND_ASYNC_EXCEPTION_DEFAULT:
			return async_ce_async_exception;
		case ZEND_ASYNC_EXCEPTION_CANCELLATION:
			return async_ce_cancellation;
		default:
			return NULL;
	}
}

static zend_coroutine_t *scheduler_coroutine_from_object(zend_object *object)
{
	if (object->ce != async_ce_coroutine) {
		return NULL;
	}

	return &async_coroutine_from_object(object)->coroutine;
}

/* The frame of a parked coroutine: started, not running, not finished (a yield is QUEUED). */
static zend_execute_data *scheduler_coroutine_execute_data(zend_coroutine_t *zend_coroutine)
{
	async_coroutine_t *coroutine = (async_coroutine_t *) zend_coroutine;
	const zend_coroutine_status status = ZEND_COROUTINE_STATUS(zend_coroutine);

	if (!ZEND_COROUTINE_IS_STARTED(zend_coroutine) || coroutine->fiber_context == NULL ||
		(status != ZEND_COROUTINE_STATUS_QUEUED && status != ZEND_COROUTINE_STATUS_SUSPENDED)) {
		return NULL;
	}

	return coroutine->fiber_context->execute_data;
}

static uint32_t scheduler_add_finish_handler(zend_coroutine_t *coroutine,
											 zend_coroutine_finish_handler_fn handler,
											 zend_coroutine_t *waiter,
											 void *data)
{
	return async_finish_handler_add((async_coroutine_t *) coroutine, handler, waiter, data);
}

static bool scheduler_remove_finish_handler(zend_coroutine_t *coroutine, uint32_t handler_id)
{
	return async_finish_handler_remove((async_coroutine_t *) coroutine, handler_id);
}

/* call_on_main_stack is left to the core's default, which calls the function where it is. */
static const zend_async_scheduler_api_t scheduler_api = {
	.size = sizeof(zend_async_scheduler_api_t),
	.version = ZEND_ASYNC_API_VERSION,
	.new_coroutine = scheduler_new_coroutine,
	.gc_new_coroutine = scheduler_gc_new_coroutine,
	.enqueue_coroutine = async_scheduler_enqueue,
	.suspend = scheduler_suspend,
	.cancel = scheduler_cancel,
	.launch = scheduler_launch,
	.shutdown = scheduler_shutdown,
	.get_class_ce = scheduler_get_class_ce,
	.call_on_main_stack = NULL,
	.defer = scheduler_defer,
	.coroutine_from_object = scheduler_coroutine_from_object,
	.intercept_fiber = scheduler_intercept_fiber,
	.coroutine_execute_data = scheduler_coroutine_execute_data,
	.add_switch_handler = scheduler_add_switch_handler,
	.remove_switch_handler = scheduler_remove_switch_handler,
	.add_finish_handler = scheduler_add_finish_handler,
	.remove_finish_handler = scheduler_remove_finish_handler,
	.await = scheduler_await,
	.add_awaiting_info = scheduler_add_awaiting_info,
	.remove_awaiting_info = scheduler_remove_awaiting_info,
	.get_awaiting_info = scheduler_get_awaiting_info,
};

bool async_scheduler_register(void)
{
	return zend_async_scheduler_register("true_async", &scheduler_api);
}

///////////////////////////////////////////////////////////////////
/// Request lifecycle
///////////////////////////////////////////////////////////////////

void async_scheduler_request_startup(void)
{
	circular_buffer_ctor(&ASYNC_G(run_queue), 0, sizeof(async_coroutine_t *), NULL);
	/* The run queue never shrinks (section 5). */
	ASYNC_G(run_queue).auto_optimize = false;
	circular_buffer_ctor(&ASYNC_G(fiber_context_pool), ASYNC_FIBER_POOL_SIZE, sizeof(async_fiber_context_t *), NULL);
	circular_buffer_ctor(&ASYNC_G(microtasks), 0, sizeof(zend_async_microtask_t *), NULL);
	zend_hash_init(&ASYNC_G(coroutines), 8, NULL, NULL, false);

	/* The core turns async off at every request end (main.c). */
	ZEND_ASYNC_INITIALIZE;
}

/* Runs after the core turned async off (the main and current slots are NULL already). What is left
 * in the registry is the main coroutine minted by the last from_main call; the parked coroutines a
 * bailout leaves (U6) are S3.10's. */
void async_scheduler_request_shutdown(void)
{
	async_coroutine_t *coroutine = NULL;

	ZEND_HASH_FOREACH_PTR(&ASYNC_G(coroutines), coroutine)
	{
		ZEND_ASSERT(ZEND_COROUTINE_IS_MAIN(&coroutine->coroutine) && "S3.10: unwind what a bailout left");

		efree(coroutine->fiber_context);
		coroutine->fiber_context = NULL;

		ZEND_COROUTINE_SET_STATUS(&coroutine->coroutine, ZEND_COROUTINE_STATUS_FINISHED);
		OBJ_RELEASE(&coroutine->std);
	}
	ZEND_HASH_FOREACH_END();

	zend_hash_destroy(&ASYNC_G(coroutines));

	ZEND_ASSERT(circular_buffer_is_empty(&ASYNC_G(fiber_context_pool)));
	circular_buffer_dtor(&ASYNC_G(run_queue));
	circular_buffer_dtor(&ASYNC_G(fiber_context_pool));

	/* A microtask that never got its tick is released unrun, as a cancelled one (ts.c). */
	zend_async_microtask_t *microtask = NULL;

	while (circular_buffer_pop_ptr(&ASYNC_G(microtasks), (void **) &microtask) == SUCCESS) {
		ZEND_ASYNC_MICROTASK_RELEASE(microtask);
	}

	circular_buffer_dtor(&ASYNC_G(microtasks));

	if (UNEXPECTED(ZEND_ASYNC_EXIT_EXCEPTION != NULL)) {
		OBJ_RELEASE(ZEND_ASYNC_EXIT_EXCEPTION);
		ZEND_ASYNC_EXIT_EXCEPTION = NULL;
	}
}
