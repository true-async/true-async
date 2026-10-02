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
 * The scheduler behind the core's slots (dev/plans/S3.md, sections 5 and 7), on TrueAsync's hybrid
 * algorithm: the scheduler's code runs between coroutines and in a scheduler coroutine of its own,
 * both at once.
 *
 * Between coroutines: coroutines run in FIFO order from one run queue, on pooled fiber contexts.
 * Whoever gives up the CPU runs the tick (the microtasks) on its own stack, picks the next coroutine
 * and switches straight into it (section 4.2). A context outlives its coroutine. When the coroutine
 * finishes, the context's loop (fiber_entry) runs the next queued coroutine that has no context of
 * its own in place, without a switch; when the next one already has a context, it switches there
 * and parks in the pool, or ends when the pool is full.
 *
 * The scheduler coroutine has its own fiber (scheduler_fiber_entry) and takes over what has no
 * coroutine to run on: a suspend or an idle context that finds the queue empty switches into it, the
 * from_main call switches into it to drain the queue after main, and a bailout goes through it, which
 * unwinds every other coroutine (scheduler_bailout_all). It is created by the first work that needs
 * it (an enqueue, a defer, a suspend) and ends when it has drained the queue in a from_main call, or
 * after a bailout; later work creates a new one.
 *
 * The main coroutine runs on the OS thread stack under a copy of the engine's context. When the
 * script ends, the core calls the suspend slot with from_main: main finishes, the scheduler drains
 * the queue and comes back to the OS stack, and a new main coroutine is minted there, so the code the
 * core runs afterwards (shutdown functions, destructors) still has a current coroutine.
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
static ZEND_STACK_ALIGNED void scheduler_fiber_entry(zend_fiber_transfer *transfer);

///////////////////////////////////////////////////////////////////
/// Fiber contexts and their pool
///////////////////////////////////////////////////////////////////

static void fiber_context_cleanup(zend_fiber_context *context)
{
	efree((async_fiber_context_t *) context);
}

/* A new context whose first entry runs `entry`; NULL with an exception when the stack cannot be
 * allocated (zend_fiber_init_context throws). */
static async_fiber_context_t *fiber_context_create(zend_fiber_coroutine entry, const size_t stack_size)
{
	async_fiber_context_t *fiber_context = ecalloc(1, sizeof(async_fiber_context_t));
	const zend_result result = zend_fiber_init_context(&fiber_context->context, async_ce_coroutine, entry, stack_size);

	if (UNEXPECTED(result == FAILURE)) {
		efree(fiber_context);
		return NULL;
	}

	fiber_context->context.cleanup = fiber_context_cleanup;

	return fiber_context;
}

/* A context for a coroutine that has none: a parked one from the pool, else a new one. NULL when the
 * stack cannot be allocated, and the exception is then reported as uncaught: the caller ends the
 * request, as running out of memory does. This is the core's path for an exception thrown without
 * a frame (zend_throw_exception_internal) minus its user handler, deliberately: finishing the
 * coroutine unrun, or a handler that lets the request go on, leaves a full GC root buffer full, and
 * every GC coroutine started for it gets no stack either, forever. */
static async_fiber_context_t *fiber_context_take(void)
{
	async_fiber_context_t *fiber_context = NULL;

	if (circular_buffer_pop_ptr(&ASYNC_G(fiber_context_pool), (void **) &fiber_context) == SUCCESS) {
		return fiber_context;
	}

	fiber_context = fiber_context_create(fiber_entry, EG(fiber_stack_size));

	if (UNEXPECTED(fiber_context == NULL)) {
		zend_exception_error(EG(exception), E_ERROR);
	}

	return fiber_context;
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

/* Ends every parked context, from the scheduler coroutine at its end (TrueAsync's
 * fiber_pool_cleanup): each wakes with no current coroutine, leaves its loop and switches back to the
 * scheduler, where the switch destroys it. */
static void fiber_pool_teardown(void)
{
	async_fiber_context_t *fiber_context = NULL;

	while (circular_buffer_pop_ptr(&ASYNC_G(fiber_context_pool), (void **) &fiber_context) == SUCCESS) {
		ZEND_ASYNC_CURRENT_COROUTINE = NULL;

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

/* The scheduler coroutine runs from here on; returns its context, where a coroutine or a context goes
 * when it has nothing to run. It exists while any coroutine does: every coroutine was enqueued, and
 * the enqueue creates it. */
static zend_always_inline zend_fiber_context *make_scheduler_current(void)
{
	async_coroutine_t *scheduler_coroutine = ASYNC_G(scheduler_coroutine);

	ZEND_ASSERT(scheduler_coroutine != NULL && "a coroutine ran, so the scheduler exists");
	ZEND_ASYNC_CURRENT_COROUTINE = &scheduler_coroutine->coroutine;

	return &scheduler_coroutine->fiber_context->context;
}

/* Switches into `context` with `flags` (0, or ZEND_FIBER_TRANSFER_FLAG_BAILOUT to unwind it); returns
 * the flags the context that switches back hands over (only ZEND_FIBER_TRANSFER_FLAG_BAILOUT is
 * used). */
static uint8_t switch_to(zend_fiber_context *context, const uint8_t flags)
{
	zend_fiber_transfer transfer = { .context = context, .flags = flags };
	ZVAL_NULL(&transfer.value);

	zend_fiber_switch_context(&transfer);

	ZEND_ASSERT(!(transfer.flags & ZEND_FIBER_TRANSFER_FLAG_ERROR) && "errors travel in the waker, not the transfer");

	return transfer.flags;
}

/* Moves the pending exception to the request's exit exception (section 6): it has no frame to go
 * to. */
static void exception_to_exit_exception(void)
{
	zend_object *exception = EG(exception);

	GC_ADDREF(exception);
	zend_clear_exception();
	async_exit_exception_add(exception);
}

/* The scheduler's tick (section 4.2, step 3): the microtasks queued so far, in scheduler context, as
 * TrueAsync runs them on every pass of its loop. The first one that throws stops the tick, as in
 * TrueAsync, and its exception ends the request as the exit exception (section 6; the graceful
 * shutdown it starts is S3.8's); the rest wait for the next tick. The flag is restored, not cleared:
 * the scheduler coroutine ticks with it set and keeps it. */
static void scheduler_tick(void)
{
	circular_buffer_t *microtasks = &ASYNC_G(microtasks);
	zend_async_microtask_t *microtask = NULL;
	const bool was_in_scheduler_context = ZEND_ASYNC_IN_SCHEDULER_CONTEXT;

	/* A finished coroutine's release left it (a destructor of its arguments that threw): the next
	 * coroutine's call would return at once with it set. ts.c folds it before every switch. */
	if (UNEXPECTED(EG(exception) != NULL)) {
		exception_to_exit_exception();
	}

	ZEND_ASYNC_IN_SCHEDULER_CONTEXT = true;

	while (circular_buffer_pop_ptr(microtasks, (void **) &microtask) == SUCCESS) {
		if (EXPECTED(!ZEND_ASYNC_MICROTASK_IS_CANCELLED(microtask))) {
			microtask->handler(microtask);
		}

		ZEND_ASYNC_MICROTASK_RELEASE(microtask);

		if (UNEXPECTED(EG(exception) != NULL)) {
			exception_to_exit_exception();
			break;
		}
	}

	ZEND_ASYNC_IN_SCHEDULER_CONTEXT = was_in_scheduler_context;
}

/* Runs coroutines on this context until it has nothing to run and the pool does not keep it.
 * Returns the context to switch to as this one ends: the next coroutine's, or the scheduler's. */
static zend_fiber_context *run_coroutines(async_fiber_context_t *fiber_context)
{
	for (;;) {
		async_coroutine_t *coroutine = (async_coroutine_t *) ZEND_ASYNC_CURRENT_COROUTINE;

		/* The pool's teardown, on the scheduler coroutine, wakes a parked context with no current
		 * coroutine. */
		if (UNEXPECTED(coroutine == NULL)) {
			return make_scheduler_current();
		}

		ZEND_ASSERT(coroutine->fiber_context == fiber_context);
		async_coroutine_execute(coroutine);
		scheduler_tick();

		async_coroutine_t *next_coroutine = run_queue_pop();

		/* The in-place run: a coroutine that never ran takes this context, with no switch. */
		if (next_coroutine != NULL && next_coroutine->fiber_context == NULL) {
			next_coroutine->fiber_context = fiber_context;
			make_current(next_coroutine);
			continue;
		}

		zend_fiber_context *target = NULL;

		if (next_coroutine != NULL) {
			target = &next_coroutine->fiber_context->context;
			make_current(next_coroutine);
		} else {
			/* Nothing queued: the scheduler coroutine waits for what comes next (TrueAsync's
			 * fiber_entry, scheduler.c:1960-1961). */
			target = make_scheduler_current();
		}

		if (!fiber_pool_keep(fiber_context)) {
			return target;
		}

		const uint8_t flags = switch_to(target, 0);
		ZEND_ASSERT(flags == 0 && "a parked context is woken only to run a coroutine or to end");
		(void) flags;
	}
}

/* The first entry into a coroutine's context. A bailout out of a coroutine ends the context: it goes
 * with the bailout flag to the scheduler coroutine, which unwinds the other coroutines and hands the
 * bailout to the stack that owns the request (TrueAsync's fiber_entry, scheduler.c:2026-2045).
 * Nothing else runs in the catch: zend_first_try left no bailout address behind it. */
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

		target = run_coroutines(fiber_context);
	}
	zend_catch
	{
		flags = ZEND_FIBER_TRANSFER_FLAG_BAILOUT;
		target = make_scheduler_current();
	}
	zend_end_try();

	zend_vm_stack_destroy();

	/* The trampoline marks this context dead and switches to `target`, whose switch destroys it. */
	transfer->context = target;
	transfer->flags = flags;
	ZVAL_NULL(&transfer->value);
}

///////////////////////////////////////////////////////////////////
/// The scheduler coroutine
///////////////////////////////////////////////////////////////////

/* Creates the scheduler coroutine; false with an exception when its stack cannot be allocated. A
 * coroutine object, as in TrueAsync and the core's ts.c, because the core expects a current coroutine
 * while async is active (zend_fibers.c, zend_gc_collect_cycles): the scheduler is current while it
 * runs. It stays out of the registry, or it would count among the coroutines it waits for, and is
 * never enqueued. */
static bool scheduler_coroutine_create(void)
{
	/* The object first: a bailout out of its allocation leaves no mapped stack behind (ts.c). */
	async_coroutine_t *scheduler_coroutine = async_coroutine_new();
	/* Never below the core's default fiber stack: a script may shrink fiber.stack_size to nothing,
	 * and the scheduler still reports that failure. ts.c's 128 KiB floor is not enough under ASAN,
	 * whose reserved stack (zend.c, OnUpdateReservedStackSize) is ten times larger. */
	const size_t stack_size = MAX(EG(fiber_stack_size), ZEND_FIBER_DEFAULT_C_STACK_SIZE);

	scheduler_coroutine->fiber_context = fiber_context_create(scheduler_fiber_entry, stack_size);

	if (UNEXPECTED(scheduler_coroutine->fiber_context == NULL)) {
		OBJ_RELEASE(&scheduler_coroutine->std);
		return false;
	}

	ASYNC_G(scheduler_coroutine) = scheduler_coroutine;

	return true;
}

/* The scheduler coroutine for the work being added (an enqueue, a defer, a suspend), as TrueAsync
 * launches its scheduler on the first spawn or suspend; false with an exception when it cannot be
 * created. */
static zend_always_inline bool scheduler_coroutine_ensure(void)
{
	return EXPECTED(ASYNC_G(scheduler_coroutine) != NULL) || scheduler_coroutine_create();
}

/* The scheduler's VM stack, with its first page on the scheduler's own C stack, as in TrueAsync
 * (scheduler.c:1796-1829): no allocation, so the scheduler starts even after an out-of-memory
 * bailout, and the code it runs (microtasks, finalizes) has a root frame. */
static void scheduler_vm_stack_start(zend_fiber_context *context, zval *vm_stack_memory)
{
	/* Determined as zend_fiber_vm_stack_start does: an empty ini value means "never configured". */
	zend_long error_reporting = zend_ini_long_literal("error_reporting");

	if (UNEXPECTED(!error_reporting)) {
		const zend_string *value = zend_ini_str_literal("error_reporting");

		if (UNEXPECTED(value == NULL || ZSTR_LEN(value) == 0)) {
			error_reporting = E_ALL;
		}
	}

	zend_vm_stack stack = (zend_vm_stack) vm_stack_memory;
	stack->top = ZEND_VM_STACK_ELEMENTS(stack);
	stack->end = (zval *) ((char *) vm_stack_memory + ZEND_FIBER_VM_STACK_SIZE);
	stack->prev = NULL;

	EG(vm_stack) = stack;
	EG(vm_stack_top) = stack->top + ZEND_CALL_FRAME_SLOT;
	EG(vm_stack_end) = stack->end;
	EG(vm_stack_page_size) = ZEND_FIBER_VM_STACK_SIZE;

	zend_execute_data *execute_data = (zend_execute_data *) stack->top;
	memset(execute_data, 0, sizeof(zend_execute_data));
	execute_data->func = &root_function;

	EG(current_execute_data) = execute_data;
	EG(jit_trace_num) = 0;
	EG(error_reporting) = (int) error_reporting;

#ifdef ZEND_CHECK_STACK_LIMIT
	EG(stack_base) = zend_fiber_stack_base(context->stack);
	EG(stack_limit) = zend_fiber_stack_limit(context->stack);
#else
	(void) context;
#endif
}

/* The scheduler coroutine's loop (TrueAsync's fiber_entry with is_scheduler): the tick, then a switch
 * into the next queued coroutine, until the queue and the microtasks are empty. A coroutine never
 * runs on the scheduler's own stack: one without a context gets one first. Returns true when a
 * coroutine came back with a bailout. Runs in scheduler context, cleared only around the switch. */
static bool scheduler_loop(void)
{
	for (;;) {
		scheduler_tick();

		async_coroutine_t *next_coroutine = run_queue_pop();

		if (next_coroutine == NULL) {
			/* The tick stopped at a microtask that threw; the rest run on the next pass. */
			if (UNEXPECTED(circular_buffer_is_not_empty(&ASYNC_G(microtasks)))) {
				continue;
			}

			const uint32_t waiting = zend_hash_num_elements(&ASYNC_G(coroutines));

			/* Coroutines left parked with nothing to wake them: S3.8 resolves the deadlock. Until
			 * then it ends the request; a release build would otherwise spin here. */
			if (UNEXPECTED(waiting > 0)) {
				zend_error_noreturn(E_ERROR, "Deadlock detected: %u coroutines wait and none can run", waiting);
			}

			return false;
		}

		if (next_coroutine->fiber_context == NULL) {
			next_coroutine->fiber_context = fiber_context_take();

			/* No stack: the catch below unwinds every coroutine; the popped one finishes unrun. */
			if (UNEXPECTED(next_coroutine->fiber_context == NULL)) {
				zend_bailout();
			}
		}

		make_current(next_coroutine);
		ZEND_ASYNC_IN_SCHEDULER_CONTEXT = false;

		const uint8_t flags = switch_to(&next_coroutine->fiber_context->context, 0);

		/* Whoever switched back made the scheduler current (make_scheduler_current). */
		ZEND_ASYNC_IN_SCHEDULER_CONTEXT = true;

		if (UNEXPECTED(flags & ZEND_FIBER_TRANSFER_FLAG_BAILOUT)) {
			return true;
		}
	}
}

/* The next coroutine scheduler_bailout_all ends: not main, not finished (a finalize sets FINISHED
 * before its handlers, which may bail out), not one it took already (ASYNC_COROUTINE_F_BAILOUT), so
 * every coroutine is taken once. */
static async_coroutine_t *bailout_next_coroutine(void)
{
	async_coroutine_t *coroutine = NULL;

	ZEND_HASH_FOREACH_PTR(&ASYNC_G(coroutines), coroutine)
	{
		const zend_coroutine_t *zend_coroutine = &coroutine->coroutine;

		if (!ZEND_COROUTINE_IS_MAIN(zend_coroutine) && !ZEND_COROUTINE_IS_FINISHED(zend_coroutine) &&
			!(zend_coroutine->flags & ASYNC_COROUTINE_F_BAILOUT)) {
			return coroutine;
		}
	}
	ZEND_HASH_FOREACH_END();

	return NULL;
}

/* After a bailout nothing more runs (section 4.5): every started coroutine but main is switched into
 * once with the bailout flag, so its stack unwinds through its own zend_first_try and its context
 * comes back here, and every coroutine that never started finishes with is_bailout handlers
 * (TrueAsync's bailout_all_coroutines, scheduler.c:949-995). The registry is scanned again after
 * each one, as ts.c does: a finalize removes entries and an enqueue (a GC coroutine) may add one.
 * Main is left to the scheduler's end, which hands it the bailout, as the core's ts.c does: main's
 * bailout may land in any zend_try of main.c, so the scheduler must not be parked inside this loop
 * while main unwinds. */
static void scheduler_bailout_all(void)
{
	async_coroutine_t *coroutine = NULL;

	while ((coroutine = bailout_next_coroutine()) != NULL) {
		coroutine->coroutine.flags |= ASYNC_COROUTINE_F_BAILOUT;

		if (!ZEND_COROUTINE_IS_STARTED(&coroutine->coroutine)) {
			async_coroutine_finalize(coroutine);
			continue;
		}

		/* The coroutine is finished, maybe freed, when its context comes back through fiber_entry's
		 * catch, which made the scheduler current again: not read again. */
		make_current(coroutine);
		ZEND_ASYNC_IN_SCHEDULER_CONTEXT = false;
		switch_to(&coroutine->fiber_context->context, ZEND_FIBER_TRANSFER_FLAG_BAILOUT);
		ZEND_ASYNC_IN_SCHEDULER_CONTEXT = true;
	}

	/* Every coroutine but main is finished, and the finalizes may have freed some that are still
	 * queued (a waiter woken by an unwound coroutine): the entries are dropped unread. A yielded
	 * main's entry goes too; main gets the bailout instead. */
	circular_buffer_clean(&ASYNC_G(run_queue));
}

/* The scheduler coroutine's fiber. It runs the loop, or only the unwinding when it is entered with a
 * bailout, then ends: the parked contexts go, its object is released, and control goes to the stack
 * that owns the request: a parked main with the bailout, else the OS stack (the from_main call that
 * entered it, or main.c's catch after main's own bailout). */
static ZEND_STACK_ALIGNED void scheduler_fiber_entry(zend_fiber_transfer *transfer)
{
	async_coroutine_t *scheduler_coroutine = ASYNC_G(scheduler_coroutine);
	bool is_bailout = (transfer->flags & ZEND_FIBER_TRANSFER_FLAG_BAILOUT) != 0;
	zval vm_stack_memory[ZEND_FIBER_VM_STACK_SIZE / sizeof(zval)];

	ZEND_ASSERT(ZEND_ASYNC_CURRENT_COROUTINE == &scheduler_coroutine->coroutine);

	EG(vm_stack) = NULL;

	zend_first_try
	{
		ZEND_ASYNC_IN_SCHEDULER_CONTEXT = true;
		scheduler_vm_stack_start(&scheduler_coroutine->fiber_context->context, vm_stack_memory);

		if (EXPECTED(!is_bailout)) {
			is_bailout = scheduler_loop();
		}

		if (UNEXPECTED(is_bailout)) {
			scheduler_bailout_all();
		}
	}
	zend_catch
	{
		/* A bailout on this stack: a microtask, a finalize, the deadlock. */
		is_bailout = true;
		ZEND_ASYNC_IN_SCHEDULER_CONTEXT = true;
		ZEND_ASYNC_CURRENT_COROUTINE = &scheduler_coroutine->coroutine;
		scheduler_bailout_all();
	}
	zend_end_try();

	fiber_pool_teardown();

	/* The context is destroyed by the switch out of it; the next work creates a new scheduler. */
	ASYNC_G(scheduler_coroutine) = NULL;
	scheduler_coroutine->fiber_context = NULL;
	ZEND_ASYNC_CURRENT_COROUTINE = NULL;
	OBJ_RELEASE(&scheduler_coroutine->std);

	ZEND_ASYNC_IN_SCHEDULER_CONTEXT = false;

	async_coroutine_t *main_coroutine = (async_coroutine_t *) ZEND_ASYNC_MAIN_COROUTINE;

	/* Without a bailout the loop ends only when every coroutine finished, main included. */
	ZEND_ASSERT(is_bailout || main_coroutine == NULL);

	if (UNEXPECTED(main_coroutine != NULL)) {
		make_current(main_coroutine);
		transfer->context = &main_coroutine->fiber_context->context;
	} else {
		transfer->context = EG(main_fiber_context);
	}

	transfer->flags = is_bailout ? ZEND_FIBER_TRANSFER_FLAG_BAILOUT : 0;
	ZVAL_NULL(&transfer->value);

	/* The first page is on this stack; the pages the VM added are freed (TrueAsync, :2083-2093). */
	zend_vm_stack page = EG(vm_stack);

	while (page != NULL && page->prev != NULL) {
		zend_vm_stack older_page = page->prev;
		efree(page);
		page = older_page;
	}

	EG(vm_stack) = NULL;
}

///////////////////////////////////////////////////////////////////
/// The main coroutine
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

/* The suspend slot's from_main calls (section 7): main finishes, the scheduler coroutine drains the
 * queue on its own stack and comes back here, a new main is minted on this stack. A bailout, the
 * call's or the drain's, is re-raised on the way out after the new main exists, so whatever runs
 * after the core's catch has a current coroutine. */
static bool scheduler_main_suspend(const bool is_bailout)
{
	async_coroutine_t *main_coroutine = (async_coroutine_t *) ZEND_ASYNC_MAIN_COROUTINE;
	bool bailout = is_bailout;

	/* A bailout out of the previous call's main_coroutine_finish left no main to finish. */
	if (EXPECTED(main_coroutine != NULL)) {
		main_coroutine_finish(main_coroutine, is_bailout);
	}

	/* No scheduler: nothing was queued or deferred since the last one ended. */
	if (ASYNC_G(scheduler_coroutine) != NULL) {
		zend_fiber_context *scheduler_context = make_scheduler_current();
		const uint8_t flags = is_bailout ? ZEND_FIBER_TRANSFER_FLAG_BAILOUT : 0;

		bailout = (switch_to(scheduler_context, flags) & ZEND_FIBER_TRANSFER_FLAG_BAILOUT) != 0;
	}

	ZEND_ASSERT(circular_buffer_is_empty(&ASYNC_G(fiber_context_pool)));

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

	ZEND_ASSERT(coroutine != ASYNC_G(scheduler_coroutine) && "the scheduler coroutine is never queued");

	if (UNEXPECTED(!scheduler_coroutine_ensure())) {
		if (error != NULL && transfer_error) {
			OBJ_RELEASE(error);
		}

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

	/* A park from the tick would leave the tick halfway (4.6). */
	if (UNEXPECTED(ZEND_ASYNC_IN_SCHEDULER_CONTEXT)) {
		async_wait_unlink(coroutine);
		zend_throw_error(NULL, "A coroutine cannot be stopped from the Scheduler context");
		return false;
	}

	/* Inside a Fiber the scheduler did not adopt (until S3.9) the stack is the Fiber's: parking it as
	 * this coroutine would resume the coroutine inside the Fiber later. */
	if (UNEXPECTED(EG(current_fiber_context) != &coroutine->fiber_context->context)) {
		async_wait_unlink(coroutine);
		zend_throw_error(NULL, "Cannot switch coroutines in the current execution context");
		return false;
	}

	/* The queue may be empty below, and the scheduler coroutine is where the suspend goes then: a
	 * coroutine always has one (its enqueue made it), main may not yet. */
	if (UNEXPECTED(!scheduler_coroutine_ensure())) {
		async_wait_unlink(coroutine);
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
		async_coroutine_t *next_coroutine = run_queue_pop();

		/* Nothing queued: the scheduler coroutine waits for what comes next (TrueAsync's
		 * scheduler_next_tick, scheduler.c:1610-1613). */
		if (next_coroutine == NULL) {
			if (UNEXPECTED(switch_to(make_scheduler_current(), 0) & ZEND_FIBER_TRANSFER_FLAG_BAILOUT)) {
				async_wait_unlink(coroutine);
				zend_bailout();
			}

			continue;
		}

		/* A yield with nobody ahead (B3). */
		if (next_coroutine == coroutine) {
			ZEND_COROUTINE_SET_STATUS(zend_coroutine, ZEND_COROUTINE_STATUS_RUNNING);
			break;
		}

		if (next_coroutine->fiber_context == NULL) {
			next_coroutine->fiber_context = fiber_context_take();

			/* No stack: the request ends through the scheduler, which unwinds every coroutine, this one
			 * and the popped one included, and drops the queue. A bailout raised here first would land
			 * in whatever zend_try this stack has (a shutdown function's) and leave the queue behind. */
			if (UNEXPECTED(next_coroutine->fiber_context == NULL)) {
				switch_to(make_scheduler_current(), ZEND_FIBER_TRANSFER_FLAG_BAILOUT);
				async_wait_unlink(coroutine);
				zend_bailout();
			}
		}

		make_current(next_coroutine);

		/* The scheduler hands the bailout to a parked coroutine (scheduler_bailout_all) or to a parked
		 * main (its end): it is re-raised on this stack, which unwinds through its own try. */
		if (UNEXPECTED(switch_to(&next_coroutine->fiber_context->context, 0) & ZEND_FIBER_TRANSFER_FLAG_BAILOUT)) {
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

/* The queue takes the caller's reference; the tick releases it. The scheduler coroutine runs the
 * tick when nothing else does (after main); on false the caller keeps its reference. */
static bool scheduler_defer(zend_async_microtask_t *microtask)
{
	if (UNEXPECTED(!scheduler_coroutine_ensure())) {
		return false;
	}

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
	ASYNC_G(scheduler_coroutine) = NULL;

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

	/* A scheduler created after the last from_main call that entered one (a bailout out of that call
	 * before it switched, say) was never entered: its stack is unmapped here. A parked one is S3.10's
	 * (U6). */
	async_coroutine_t *scheduler_coroutine = ASYNC_G(scheduler_coroutine);

	if (UNEXPECTED(scheduler_coroutine != NULL)) {
		ZEND_ASSERT(scheduler_coroutine->fiber_context->context.status == ZEND_FIBER_STATUS_INIT &&
					"S3.10: unwind what a bailout left");

		ASYNC_G(scheduler_coroutine) = NULL;
		zend_fiber_destroy_context(&scheduler_coroutine->fiber_context->context);
		scheduler_coroutine->fiber_context = NULL;
		OBJ_RELEASE(&scheduler_coroutine->std);
	}

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
