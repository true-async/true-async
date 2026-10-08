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
#include "SAPI.h"
#include "php_true_async.h"
#include "Zend/zend_observer.h"
#include "scheduler.h"
#include "await.h"
#include "channel.h"
#include "coroutine.h"
#include "collector.h"
#include "exceptions.h"
#include "scope.h"
#include "Zend/zend_smart_str.h"
#include "internal/circular_buffer.h"
#include "test_hooks.h"

/* Contexts the pool keeps however short the run queue is: TrueAsync's policy (D23) with 1024 for its
 * 4. A chain of awaits up to that deep, or that many waiters woken together, then take no new stack:
 * B4 at depth 100 runs 21 % fewer instructions per link, B5 (1000 waiters) 11 % fewer and in a tenth
 * of the time (dev/BENCHMARKS.md). A pooled context keeps about 20 KiB of its stack resident and
 * 0.14 KiB of the request's memory, until the scheduler ends. */
#define ASYNC_FIBER_POOL_SIZE 1024

/* The bottom frame of every coroutine's VM stack. Nameless: backtraces skip a frame without a
 * function name, as they skip the core's own fiber root frame. */
static zend_function root_function = { ZEND_INTERNAL_FUNCTION };

static ZEND_STACK_ALIGNED void fiber_entry(zend_fiber_transfer *transfer);
static ZEND_STACK_ALIGNED void scheduler_fiber_entry(zend_fiber_transfer *transfer);
static void scheduler_cancel_all(zend_object *cancellation);
static void context_vm_stack_start(zend_fiber_context *context, zval *vm_stack_memory);
static void context_vm_stack_free(void);
static void exit_deadline_withdraw(void);

///////////////////////////////////////////////////////////////////
/// Fiber contexts and their pool
///////////////////////////////////////////////////////////////////

static void fiber_context_cleanup(zend_fiber_context *context)
{
	efree((async_fiber_context_t *) context);
}

/* A new context whose first entry runs `entry_function`; NULL with an exception when the stack cannot be
 * allocated (zend_fiber_init_context throws). */
static async_fiber_context_t *fiber_context_create(zend_fiber_coroutine entry_function, const size_t stack_size)
{
	async_fiber_context_t *fiber_context = ecalloc(1, sizeof(async_fiber_context_t));
	const zend_result result =
			zend_fiber_init_context(&fiber_context->context, async_ce_coroutine, entry_function, stack_size);

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

	/* fiber_entry keeps the first VM stack page on the stack (context_vm_stack_start): it gets its
	 * own room, so fiber.stack_size stays the C budget the core's stack limit measures, as for a Fiber;
	 * without it a small size overran into the guard page. The room would also hide the core's
	 * refusal of a small stack, so it is made here with the core's formula (zend_fiber_stack_allocate). */
	const size_t page_size = zend_get_page_size();
	const size_t minimum_stack_size = page_size +
			ZEND_FIBER_GUARD_PAGES * page_size
#ifdef __SANITIZE_ADDRESS__
					* 6
#endif
			;

	if (UNEXPECTED(EG(fiber_stack_size) < minimum_stack_size)) {
		zend_throw_exception_ex(
				NULL, 0, "Fiber stack size is too small, it needs to be at least %zu bytes", minimum_stack_size);
	} else {
		fiber_context = fiber_context_create(fiber_entry, EG(fiber_stack_size) + ZEND_FIBER_VM_STACK_SIZE);
	}

	/* The report runs PHP code (the exception's __toString, a release that fills the GC buffer) on a
	 * stack in the middle of a switch: in scheduler context, so a GC defers and a wait refuses. */
	if (UNEXPECTED(fiber_context == NULL)) {
		const bool was_in_scheduler_context = ZEND_ASYNC_IN_SCHEDULER_CONTEXT;
		ZEND_ASYNC_IN_SCHEDULER_CONTEXT = true;
		zend_exception_error(EG(exception), E_ERROR);
		ZEND_ASYNC_IN_SCHEDULER_CONTEXT = was_in_scheduler_context;
	}

	return fiber_context;
}

/* Parks a context that has nothing to run, or refuses when the pool has enough (D23): it keeps
 * ASYNC_FIBER_POOL_SIZE contexts, and more while the run queue is longer than the pool. */
static bool fiber_pool_keep(async_fiber_context_t *fiber_context)
{
	const size_t pooled = circular_buffer_count(&ASYNC_G(fiber_context_pool));

	if (UNEXPECTED(pooled >= ASYNC_FIBER_POOL_SIZE && pooled >= circular_buffer_count(&ASYNC_G(run_queue)))) {
		return false;
	}

	circular_buffer_push_ptr_with_resize(&ASYNC_G(fiber_context_pool), fiber_context);

	return true;
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

/* The waker keeps one error until the switch-in, by TrueAsync's rules (the fork's
 * zend_async_waker_apply_error, zend_async_API.c:1334-1373, and async_coroutine_resume,
 * coroutine.c:807-829): a new error goes on top, with the pending one as its previous, except that a
 * cancellation never replaces a pending cancellation, and a wake never brings a cancellation over a
 * pending error. An exit object wins over any other error and is never chained, unlike TrueAsync's
 * (a Fiber dropped while a throw() into it waits). Takes a reference to `error`. */
static void waker_apply_error(async_coroutine_t *coroutine, zend_object *error, const bool for_cancellation)
{
	zend_object *pending_error = coroutine->waker.error;

	if (EXPECTED(pending_error == NULL)) {
		coroutine->waker.error = error;
		return;
	}

	if (UNEXPECTED(async_is_exit_object(pending_error))) {
		OBJ_RELEASE(error);
		return;
	}

	if (UNEXPECTED(async_is_exit_object(error))) {
		coroutine->waker.error = error;
		OBJ_RELEASE(pending_error);
		return;
	}

	const bool is_dropped = for_cancellation ? instanceof_function(pending_error->ce, async_ce_cancellation)
											 : instanceof_function(error->ce, async_ce_cancellation);

	if (UNEXPECTED(is_dropped)) {
		OBJ_RELEASE(error);
		return;
	}

	zend_exception_set_previous(error, pending_error);
	coroutine->waker.error = error;
}

/* The pending exception has no frame to go to: it ends the request (section 6). An exit() is no
 * exception to report: it cancels the coroutines, as in a coroutine (D16). */
static void exception_to_exit_exception(void)
{
	zend_object *exception = EG(exception);

	if (UNEXPECTED(zend_is_unwind_exit(exception))) {
		zend_clear_exception();
		async_scheduler_cancel_for_exit();
		return;
	}

	GC_ADDREF(exception);
	zend_clear_exception();
	async_scheduler_exit_with(exception);
}

/* Throws a waker's error as TrueAsync's async_rethrow_exception does: the clear that takes it gives a
 * frame parked in the middle of an opline the opline the throw saved. With no frame (the request's
 * shutdown destructors) a throw would end the request as uncaught, and there is no opline to give
 * back: the error is stored. */
static void waker_error_throw(zend_object *error)
{
	if (EXPECTED(EG(current_execute_data) != NULL)) {
		zend_throw_exception_internal(error);
	} else {
		EG(exception) = error;
	}
}

/* A coroutine woken with an error before it ran (a cancellation) has no body to run and needs no
 * stack: it finishes where it is popped, with the error as the outcome (TrueAsync's IGNORED path, scheduler.c:507-513,
 * coroutine.c:466-499). It is current while it finishes, outside scheduler context, as when a body
 * ends, whoever pops it. What its releases throw is folded as the tick folds it: the next
 * coroutine's call would return at once with it set. */
static void coroutine_finish_unrun(async_coroutine_t *coroutine)
{
	zend_coroutine_t *previous_coroutine = ZEND_ASYNC_CURRENT_COROUTINE;
	const bool was_in_scheduler_context = ZEND_ASYNC_IN_SCHEDULER_CONTEXT;

	ZEND_ASSERT(coroutine->waker.error != NULL && EG(exception) == NULL);

	/* finalize takes it as a thrown one: an exit or a cancellation by the rules of an outcome. The pop
	 * runs on a stack whose frame may be in the middle of an opline (main parked by a collection
	 * inside a handler). */
	zend_object *error = coroutine->waker.error;
	coroutine->waker.error = NULL;
	waker_error_throw(error);

	ZEND_ASYNC_IN_SCHEDULER_CONTEXT = false;
	ZEND_ASYNC_CURRENT_COROUTINE = &coroutine->coroutine;
	async_coroutine_finalize(coroutine);
	ZEND_ASYNC_CURRENT_COROUTINE = previous_coroutine;

	/* Set before the fold, as in the tick: the release of the exception may start a collection. */
	ZEND_ASYNC_IN_SCHEDULER_CONTEXT = true;

	if (UNEXPECTED(EG(exception) != NULL)) {
		exception_to_exit_exception();
	}

	ZEND_ASYNC_IN_SCHEDULER_CONTEXT = was_in_scheduler_context;
}

/* The next coroutine to run, or NULL when the queue is empty; the ones woken with an error before they
 * ran finish on the way. */
static async_coroutine_t *run_queue_pop(void)
{
	async_coroutine_t *coroutine = NULL;

	for (;;) {
#ifdef TRUE_ASYNC_FUZZ
		/* TrueAsync's next_coroutine (scheduler.c:442-452): another queued coroutine may go first. */
		const size_t queued = circular_buffer_count(&ASYNC_G(run_queue));
		const uint32_t position = async_fuzz_scheduler_pick(&ASYNC_G(fuzz), (uint32_t) queued);

		circular_buffer_swap_ptr_at(&ASYNC_G(run_queue), 0, position);
#endif

		if (UNEXPECTED(circular_buffer_pop_ptr(&ASYNC_G(run_queue), (void **) &coroutine) == FAILURE)) {
			return NULL;
		}

		/* Any error a coroutine that never ran was enqueued with is its outcome, a cancellation or not: the
		 * core skips the body of a first entry that carries an error (zend_async_API.h, the STARTED flag),
		 * test_scheduler.c too, and TrueAsync throws it before the body (coroutine.c:513-520). */
		if (EXPECTED(ZEND_COROUTINE_IS_STARTED(&coroutine->coroutine) || coroutine->waker.error == NULL)) {
			return coroutine;
		}

		coroutine_finish_unrun(coroutine);
	}
}

/* The coroutine runs from here on: the current-coroutine slot is how its context learns whom it
 * runs. */
static zend_always_inline void make_current(async_coroutine_t *coroutine)
{
	ZEND_COROUTINE_SET_STATUS(&coroutine->coroutine, ZEND_COROUTINE_STATUS_RUNNING);
	ZEND_ASYNC_CURRENT_COROUTINE = &coroutine->coroutine;
}

/* The scheduler coroutine runs from here on; returns its context, where a coroutine or a context goes
 * when it has nothing to run. It exists while a coroutine other than main runs: every such coroutine
 * was enqueued, and the enqueue creates it; main's own calls create it first
 * (scheduler_coroutine_ensure). */
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

/* The scheduler's tick (section 4.2, step 3): the microtasks queued so far, then the reactor's
 * completions, in scheduler context, as TrueAsync runs both on every pass of its loop. The first
 * microtask that throws stops the microtasks, as in TrueAsync, and its exception ends the request
 * (section 6); the rest wait for the next tick. So does an exception a completion's notify left. The
 * flag is restored, not cleared: the scheduler coroutine ticks with it set and keeps it. */
static void scheduler_tick(const uint64_t poll_interval)
{
	circular_buffer_t *microtasks = &ASYNC_G(microtasks);
	/* Read with the microtasks: a shared module's ZTS global costs a __tls_get_addr() call after
	 * each microtask's. */
	async_reactor_t *reactor = &ASYNC_G(reactor);
	zend_async_microtask_t *microtask = NULL;
	zend_object **exception_ptr = &EG(exception);
	const bool was_in_scheduler_context = ZEND_ASYNC_IN_SCHEDULER_CONTEXT;

	/* Set before the fold: the release of the exception may start a collection, which must not wait
	 * here. */
	ZEND_ASYNC_IN_SCHEDULER_CONTEXT = true;

	/* A finished coroutine's release left it (a destructor of its arguments that threw): the next
	 * coroutine's call would return at once with it set. The core's reference scheduler,
	 * ext/test_scheduler/test_scheduler.c, folds it before every switch. */
	if (UNEXPECTED(*exception_ptr != NULL)) {
		exception_to_exit_exception();
	}

	while (circular_buffer_pop_ptr(microtasks, (void **) &microtask) == SUCCESS) {
		if (EXPECTED(!ZEND_ASYNC_MICROTASK_IS_CANCELLED(microtask))) {
			microtask->handler(microtask);
		}

		ZEND_ASYNC_MICROTASK_RELEASE(microtask);

		if (UNEXPECTED(*exception_ptr != NULL)) {
			exception_to_exit_exception();
			break;
		}
	}

	/* A request with no queue never reads the clock. */
	if (UNEXPECTED(reactor->queue != NULL)) {
		async_reactor_poll_due(reactor, poll_interval);

		if (UNEXPECTED(*exception_ptr != NULL)) {
			exception_to_exit_exception();
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

		/* The tick and the pop are scheduler work: the scheduler coroutine is current for them, as in
		 * test_scheduler.c. With none, a collection there would not defer (zend_gc_collect_cycles()
		 * compares the current coroutine with the GC's, both NULL) and its destructors would suspend
		 * nobody. */
		make_scheduler_current();
		scheduler_tick(0);

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

		if (UNEXPECTED(!fiber_pool_keep(fiber_context))) {
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
	zval vm_stack_memory[ZEND_FIBER_VM_STACK_SIZE / sizeof(zval)];

	ZEND_ASSERT(transfer->flags == 0 && "a context starts only to run a coroutine");

	/* The switcher's VM stack is saved with its state; a bailout before ours exists destroys none. */
	EG(vm_stack) = NULL;
	/* A context entered for the first time inherits the switcher's active fiber (the core restores it
	 * only on a switch back, zend_fibers.c, zend_fiber_switch_context): a coroutine started from a
	 * fiber's body would see that Fiber as Fiber::getCurrent(), and keep it after the Fiber is gone. */
	EG(active_fiber) = NULL;

	zend_first_try
	{
		/* The root frame has no caller: a coroutine's backtrace ends in it. */
		context_vm_stack_start(&fiber_context->context, vm_stack_memory);

		target = run_coroutines(fiber_context);
	}
	zend_catch
	{
		flags = ZEND_FIBER_TRANSFER_FLAG_BAILOUT;
		target = make_scheduler_current();
	}
	zend_end_try();

	context_vm_stack_free();

	/* The trampoline marks this context dead and switches to `target`, whose switch destroys it. */
	transfer->context = target;
	transfer->flags = flags;
	ZVAL_NULL(&transfer->value);
}

///////////////////////////////////////////////////////////////////
/// The scheduler coroutine
///////////////////////////////////////////////////////////////////

/* Creates the scheduler coroutine; false with an exception when its stack cannot be allocated. A
 * coroutine object, as in TrueAsync and the core's test_scheduler.c, because the core expects a current coroutine
 * while async is active (zend_fibers.c, zend_gc_collect_cycles): the scheduler is current while it
 * runs. It stays out of the registry, or it would count among the coroutines it waits for, and is
 * never enqueued. */
static bool scheduler_coroutine_create(void)
{
	/* The object first: a bailout out of its allocation leaves no mapped stack behind (the core's test_scheduler.c). */
	async_coroutine_t *scheduler_coroutine =
			async_coroutine_from_object(async_ce_coroutine->create_object(async_ce_coroutine));
	/* Never below the core's default fiber stack: a script may shrink fiber.stack_size to nothing,
	 * and the scheduler still reports that failure. test_scheduler.c's 128 KiB floor is not enough under ASAN,
	 * whose reserved stack (zend.c, OnUpdateReservedStackSize) is ten times larger. The first VM page
	 * sits on this stack too (scheduler_fiber_entry) and gets its own room, as in fiber_context_take. */
	const size_t stack_size = MAX(EG(fiber_stack_size), ZEND_FIBER_DEFAULT_C_STACK_SIZE) + ZEND_FIBER_VM_STACK_SIZE;

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

zend_long async_ini_error_reporting(void)
{
	zend_long error_reporting = zend_ini_long_literal("error_reporting");

	if (UNEXPECTED(!error_reporting)) {
		const zend_string *value = zend_ini_str_literal("error_reporting");

		if (UNEXPECTED(value == NULL || ZSTR_LEN(value) == 0)) {
			error_reporting = E_ALL;
		}
	}

	return error_reporting;
}

/* A context's VM stack, with its first page on the context's own C stack, as TrueAsync's fiber_entry
 * does for every context (scheduler.c:1796-1829): no allocation, so the scheduler starts even after
 * an out-of-memory bailout, and a context, pooled or running, holds no page of the request's memory
 * (the core's zend_fiber_vm_stack_start allocates 16 KiB). The root frame has no caller. The pages
 * the VM adds go with context_vm_stack_free(). */
static void context_vm_stack_start(zend_fiber_context *context, zval *vm_stack_memory)
{
	const zend_long error_reporting = async_ini_error_reporting();

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

/* Frees the pages the VM added to a stack context_vm_stack_start() began; the first page goes with
 * the C stack (TrueAsync's scheduler.c:2083-2093). */
static void context_vm_stack_free(void)
{
	zend_vm_stack page = EG(vm_stack);

	while (page != NULL && page->prev != NULL) {
		zend_vm_stack older_page = page->prev;
		efree(page);
		page = older_page;
	}

	EG(vm_stack) = NULL;
}

/* The deadlock report of true_async.debug_deadlock (TrueAsync's dump_deadlock_info,
 * scheduler.c:693-747): every waiting coroutine and what it waits for. Composed first and printed
 * once: an output handler run by the print may start a collection, whose coroutine joins the
 * registry being walked. */
static void scheduler_deadlock_report(const uint32_t waiting)
{
	smart_str report = { 0 };
	async_coroutine_t *coroutine = NULL;

	smart_str_append_printf(&report, "\n=== DEADLOCK REPORT START ===\nCoroutines waiting: %u\n\n", waiting);

	ZEND_HASH_FOREACH_PTR(&ASYNC_G(coroutines), coroutine)
	{
		/* Not a waiter (registry_waiting_count). */
		if (UNEXPECTED(ZEND_COROUTINE_STATUS(&coroutine->coroutine) == ZEND_COROUTINE_STATUS_CREATED)) {
			continue;
		}

		const zend_string *spawn_file = coroutine->coroutine.filename;

		smart_str_append_printf(&report,
								"Coroutine %u spawned at %s:%" PRIu32,
								coroutine->std.handle,
								spawn_file != NULL ? ZSTR_VAL(spawn_file) : "",
								coroutine->coroutine.lineno);

		const zend_execute_data *suspend_frame = async_coroutine_suspend_frame(coroutine);

		if (suspend_frame != NULL) {
			smart_str_append_printf(&report,
									", suspended at %s:%" PRIu32,
									ZSTR_VAL(suspend_frame->func->op_array.filename),
									suspend_frame->opline->lineno);
		}

		zend_array *awaiting_info = ZEND_ASYNC_GET_AWAITING_INFO(&coroutine->coroutine);

		if (awaiting_info == NULL) {
			smart_str_appends(&report, "\n  waiting for: <nothing>\n\n");
			continue;
		}

		smart_str_appends(&report, "\n  waiting for:\n");

		const zval *line = NULL;

		ZEND_HASH_FOREACH_VAL(awaiting_info, line)
		{
			smart_str_append_printf(&report, "    - %s\n", Z_STRVAL_P(line));
		}
		ZEND_HASH_FOREACH_END();

		smart_str_appendc(&report, '\n');
		zend_array_release(awaiting_info);
	}
	ZEND_HASH_FOREACH_END();

	smart_str_appends(&report, "=== DEADLOCK REPORT END   ===\n\n");
	smart_str_0(&report);

	/* Written where php_error_cb (main/main.c) writes the error it explains: display_errors=stderr sends
	 * the command-line SAPIs' errors to stderr, not to the response. */
	if (UNEXPECTED(PG(display_errors) == PHP_DISPLAY_ERRORS_STDERR &&
				   (strcmp(sapi_module.name, "cli") == 0 || strcmp(sapi_module.name, "cgi") == 0 ||
					strcmp(sapi_module.name, "phpdbg") == 0))) {
		fwrite(ZSTR_VAL(report.s), 1, ZSTR_LEN(report.s), stderr);
#ifdef PHP_WIN32
		fflush(stderr);
#endif
	} else {
		PHPWRITE(ZSTR_VAL(report.s), ZSTR_LEN(report.s));
	}

	smart_str_free(&report);
}

/* The coroutines of the registry that wait. A CREATED one is a core coroutine whose enqueue the
 * scheduler refused (scheduler.h, async_coroutine_new): creation and enqueue never straddle a switch,
 * so with the queue empty nothing else is CREATED. It waits for nothing and is not counted, as
 * test_scheduler.c counts only suspended coroutines; a deadlock's cancelling walks still cancel it,
 * which finishes it unrun. */
static uint32_t registry_waiting_count(void)
{
	const async_coroutine_t *coroutine = NULL;
	uint32_t waiting = 0;

	ZEND_HASH_FOREACH_PTR(&ASYNC_G(coroutines), coroutine)
	{
		if (EXPECTED(ZEND_COROUTINE_STATUS(&coroutine->coroutine) != ZEND_COROUTINE_STATUS_CREATED)) {
			waiting++;
		}
	}
	ZEND_HASH_FOREACH_END();

	return waiting;
}

/* The next coroutine of a cancelling walk over the registry, or NULL at its end. A cancel releases
 * errors, whose release may start a collection; it defers in scheduler context, and the creation of
 * its GC coroutine adds it to the registry under the walk: the engine's iterator follows the table
 * through a resize, as foreach by reference does (ext/standard/array.c, php_array_walk). Each walk
 * takes at most the count present at its start, as TrueAsync's foreach visits only the coroutines it
 * started with; a coroutine added under the walk is run by the queue instead. */
static async_coroutine_t *registry_walk_next(const uint32_t iterator)
{
	HashTable *coroutines = &ASYNC_G(coroutines);
	HashPosition position = zend_hash_iterator_pos(iterator, coroutines);
	async_coroutine_t *coroutine = zend_hash_get_current_data_ptr_ex(coroutines, &position);

	if (EXPECTED(coroutine != NULL)) {
		zend_hash_move_forward_ex(coroutines, &position);
		EG(ht_iterators)[iterator].pos = position;
	}

	return coroutine;
}

/* The cancel of a walk over the registry, protection cleared: whoever walks the registry needs no
 * reference to the coroutine, so the collector leaves these walks out (dev/plans/S7.md, section 2),
 * and its oracle excuses what they wake. */
static void registry_cancel(async_coroutine_t *coroutine, zend_object *error, const bool transfer_error)
{
	coroutine->coroutine.flags &= ~ASYNC_COROUTINE_F_PROTECTED;
#ifdef TRUE_ASYNC_TEST_HOOKS
	coroutine->coroutine.flags |= ASYNC_COROUTINE_F_HANDED_OUT;
#endif
	async_coroutine_cancel(coroutine, error, transfer_error, false);
}

/* A fiber parked in Fiber::suspend(): it handed control back to whoever resumed it, and only that
 * code, not an event, can wake it (S3.md section 6, D6; the core sets the status in
 * zend_fiber_coroutine_yield). */
static zend_always_inline bool coroutine_is_suspended_fiber(const async_coroutine_t *coroutine)
{
	const zend_fiber *fiber = coroutine->coroutine.extended_data;

	return ZEND_COROUTINE_IS_FIBER(&coroutine->coroutine) && fiber != NULL &&
			fiber->context.status == ZEND_FIBER_STATUS_SUSPENDED;
}

/* Nothing is queued, no microtask is pending, and `waiting` coroutines are parked with nothing left
 * to wake them (S3.md section 6, TrueAsync's resolve_deadlocks, scheduler.c:749-889): a DeadlockError
 * becomes the exit exception, and every one of them is cancelled with AsyncCancellation("Deadlock
 * detected"), protection cleared, so each runs its cleanup. Not a graceful shutdown: a coroutine that
 * catches the cancellation runs on. When every one is a suspended fiber, it is no deadlock: a worker
 * fiber left suspended (Revolt's) is closed with a graceful exit, as a dropped Fiber is, and nothing
 * is reported (scheduler.c:776-817). The loop runs in scheduler context. */
static void scheduler_resolve_deadlock(const uint32_t waiting)
{
	async_coroutine_t *coroutine = NULL;
	uint32_t suspended_fibers = 0;

	ZEND_HASH_FOREACH_PTR(&ASYNC_G(coroutines), coroutine)
	{
		if (coroutine_is_suspended_fiber(coroutine)) {
			suspended_fibers++;
		}
	}
	ZEND_HASH_FOREACH_END();

	if (UNEXPECTED(suspended_fibers == waiting)) {
		const uint32_t iterator = zend_hash_iterator_add(&ASYNC_G(coroutines), 0);
		uint32_t remaining = zend_hash_num_elements(&ASYNC_G(coroutines));

		while (remaining-- > 0 && (coroutine = registry_walk_next(iterator)) != NULL) {
			/* Protection cleared as for a deadlock: a deferred cancellation would never come, and the
			 * loop would find the same fibers again. */
			registry_cancel(coroutine, zend_create_graceful_exit(), true);
		}

		zend_hash_iterator_del(iterator);

		return;
	}

	/* The report names script paths, so it is shown only where the error it explains is. That error
	 * is raised on main's stack: error_reporting is read from INI, not from this stack's own copy. */
	if (EXPECTED(ASYNC_G(debug_deadlock) && PG(display_errors) && (async_ini_error_reporting() & E_ERROR))) {
		scheduler_deadlock_report(waiting);

		/* An output handler that threw (the report runs in scheduler context, where waits refuse). An
		 * exit() there is no exception to report, as in the tick. */
		if (UNEXPECTED(EG(exception) != NULL)) {
			zend_object *report_exception = EG(exception);

			if (UNEXPECTED(zend_is_unwind_exit(report_exception))) {
				zend_clear_exception();
			} else {
				GC_ADDREF(report_exception);
				zend_clear_exception();
				async_exit_exception_add(report_exception);
			}
		}
	}

	async_exit_exception_add(async_new_exception(
			async_ce_deadlock_error, "Deadlock detected: no active coroutines, %u coroutines in waiting", waiting));

	/* One cancellation for each coroutine, as TrueAsync's resolve_deadlocks (scheduler.c:860-879): a
	 * pending error is chained under it, which must not reach another coroutine. */
	const uint32_t iterator = zend_hash_iterator_add(&ASYNC_G(coroutines), 0);
	uint32_t remaining = zend_hash_num_elements(&ASYNC_G(coroutines));

	while (remaining-- > 0 && (coroutine = registry_walk_next(iterator)) != NULL) {
		registry_cancel(coroutine, async_new_exception(async_ce_cancellation, "Deadlock detected"), true);
	}

	zend_hash_iterator_del(iterator);
}

/* The body of the interrupt coroutine: the VM's interrupt helper (zend_vm_def.h,
 * zend_interrupt_helper), which zend_call_function also runs outside an opcode
 * (zend_execute_API.c:1196-1204). */
static void interrupt_coroutine_entry(void)
{
	atomic_store(&EG(vm_interrupt), false);

	if (atomic_load(&EG(timed_out))) {
		zend_timeout();
	} else if (zend_interrupt_function) {
		zend_interrupt_function(EG(current_execute_data));
	}
}

static bool
interrupt_coroutine_finished(zend_coroutine_t *coroutine, zend_coroutine_t *waiter, void *data, const bool is_bailout)
{
	(void) coroutine;
	(void) waiter;
	(void) data;
	(void) is_bailout;

	ASYNC_G(interrupt_coroutine) = NULL;

	return false;
}

/* A signal or a timeout that came while every coroutine waits: no opcode runs to take it, so a
 * coroutine runs the interrupt (dev/plans/S4.md 3.3), where a pcntl handler may spawn or start the
 * graceful shutdown as it does inside any coroutine. One at a time: a signal during its handler sets
 * the flag again, and the next idle pass starts the next. False when one is alive already. */
static bool scheduler_interrupt_start(void)
{
	if (ASYNC_G(interrupt_coroutine) != NULL) {
		return false;
	}

	async_coroutine_t *coroutine = async_coroutine_new();

	coroutine->coroutine.internal_entry = interrupt_coroutine_entry;
	async_finish_handler_add(&coroutine->coroutine, interrupt_coroutine_finished, NULL, NULL);
	ASYNC_G(interrupt_coroutine) = coroutine;
	async_scheduler_enqueue(&coroutine->coroutine, NULL, false);

	return true;
}

/* The scheduler coroutine's loop (TrueAsync's fiber_entry with is_scheduler): the tick, then a switch
 * into the next queued coroutine, until the queue and the microtasks are empty. A coroutine never
 * runs on the scheduler's own stack: one without a context gets one first. Returns true when a
 * coroutine came back with a bailout. Runs in scheduler context, cleared only around the switch. */
static bool scheduler_loop(void)
{
	bool shutdown_polled = false;

	for (;;) {
		scheduler_tick(0);

		async_coroutine_t *next_coroutine = run_queue_pop();

		if (next_coroutine == NULL) {
			/* The tick stopped at a microtask that threw; the rest run on the next pass. */
			if (UNEXPECTED(circular_buffer_is_not_empty(&ASYNC_G(microtasks)))) {
				continue;
			}

			if (UNEXPECTED(atomic_load(&EG(vm_interrupt))) && scheduler_interrupt_start()) {
				continue;
			}

			/* Before the parent's waits are read as this child's, by the check below and the collector. */
			if (UNEXPECTED(async_reactor_check_fork())) {
				continue;
			}

			if (async_reactor_has_waits(&ASYNC_G(reactor))) {
				/* In a graceful shutdown with no coroutine left, what still waits belongs to no coroutine,
				 * as a held signal() Future: one poll that does not block delivers what already arrived,
				 * then the loop ends and the request's shutdown closes the watches. A script that ends by
				 * itself waits on, as TrueAsync (Edmond, 2026-10-07). After a coroutine runs the poll is
				 * due again: what it started may have let more arrive. */
				if (UNEXPECTED(ASYNC_G(graceful_shutdown) && zend_hash_num_elements(&ASYNC_G(coroutines)) == 0)) {
					if (!shutdown_polled) {
						shutdown_polled = true;
						async_reactor_poll_now(&ASYNC_G(reactor));
						continue;
					}
				} else if (async_collector_idle() || async_reactor_wait_idle()) {
					continue;
				}
			}

			const uint32_t waiting = registry_waiting_count();

			if (EXPECTED(waiting == 0)) {
				exit_deadline_withdraw();
				return false;
			}

			/* Before the DeadlockError (S9-channel.md 5). */
			if (UNEXPECTED(async_channel_resolve_deadlocks())) {
				continue;
			}

			/* The cancellations queue every waiting coroutine. */
			scheduler_resolve_deadlock(waiting);
			continue;
		}

		shutdown_polled = false;

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
 * each one, as test_scheduler.c does: a finalize removes entries and a new coroutine (a GC coroutine) may add one.
 * Main is left to the scheduler's end, which hands it the bailout, as the core's test_scheduler.c does: main's
 * bailout may land in any zend_try on main's stack, so the scheduler must not be parked inside this loop
 * while main unwinds. A bailout out of the walk itself (a finish handler of a coroutine that never
 * started) lands in the walk's own try and the walk goes on: the scheduler's catch has no bailout
 * address left, and TrueAsync's walk gives up there instead. A coroutine whose finalize bailed out
 * has left the registry already and is not taken again. */
static void scheduler_bailout_all(void)
{
	bool is_done = false;

	while (!is_done) {
		zend_try
		{
			async_coroutine_t *coroutine = NULL;

			while ((coroutine = bailout_next_coroutine()) != NULL) {
				coroutine->coroutine.flags |= ASYNC_COROUTINE_F_BAILOUT;

				if (!ZEND_COROUTINE_IS_STARTED(&coroutine->coroutine)) {
					async_coroutine_finalize(coroutine);
					continue;
				}

				/* The coroutine is finished, maybe freed, when its context comes back through
				 * fiber_entry's catch, which made the scheduler current again: not read again. */
				make_current(coroutine);
				ZEND_ASYNC_IN_SCHEDULER_CONTEXT = false;
				switch_to(&coroutine->fiber_context->context, ZEND_FIBER_TRANSFER_FLAG_BAILOUT);
				ZEND_ASYNC_IN_SCHEDULER_CONTEXT = true;
			}

			is_done = true;
		}
		zend_end_try();
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

	/* As in fiber_entry: neither the switcher's VM stack nor its active fiber. */
	EG(vm_stack) = NULL;
	EG(active_fiber) = NULL;

	zend_first_try
	{
		ZEND_ASYNC_IN_SCHEDULER_CONTEXT = true;
		context_vm_stack_start(&scheduler_coroutine->fiber_context->context, vm_stack_memory);

		if (EXPECTED(!is_bailout)) {
			is_bailout = scheduler_loop();
		}
	}
	zend_catch
	{
		/* A bailout on this stack: a microtask, a finalize, the deadlock. */
		is_bailout = true;
		ZEND_ASYNC_IN_SCHEDULER_CONTEXT = true;
		ZEND_ASYNC_CURRENT_COROUTINE = &scheduler_coroutine->coroutine;
	}
	zend_end_try();

	if (UNEXPECTED(is_bailout)) {
		scheduler_bailout_all();
		/* The drain ends here too: a shutdown function's wait after the bailout is not part of it. */
		exit_deadline_withdraw();
	}

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

	context_vm_stack_free();
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
	 * scheduler context, as in test_scheduler.c. */
	ZEND_ASYNC_IN_SCHEDULER_CONTEXT = false;

	coroutine->coroutine.flags |= ZEND_COROUTINE_F_MAIN | ZEND_COROUTINE_F_STARTED;
	ZEND_COROUTINE_SET_STATUS(&coroutine->coroutine, ZEND_COROUTINE_STATUS_RUNNING);
	async_scope_add_coroutine(ASYNC_G(global_scope), coroutine);

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

/* A value is an exception to print, or, as a pointer, one printed (here, or by main.c as the exit
 * exception), which the walk skips. Each holds a reference: no other exception takes its handle. */
static void unobserved_exception_dtor(zval *value)
{
	zend_object *exception = Z_TYPE_P(value) == IS_PTR ? Z_PTR_P(value) : Z_OBJ_P(value);

	OBJ_RELEASE(exception);
}

static void scheduler_exit_exception_printed(zend_object *exception)
{
	zval value;

	GC_ADDREF(exception);
	ZVAL_PTR(&value, exception);
	zend_hash_index_update(&ASYNC_G(unobserved_exceptions), exception->handle, &value);
}

/* By the built-in Exception::__toString() or Error::__toString(): the class's override is not called. */
static void scheduler_print_exception_built_in(zend_object *exception)
{
	/* zend_exception_error() prints these without __toString(). */
	if (exception->ce == zend_ce_parse_error || exception->ce == zend_ce_compile_error) {
		GC_ADDREF(exception);
		zend_exception_error(exception, E_ERROR);
		return;
	}

	zend_class_entry *base_ce = zend_get_exception_base(exception);
	zval string, file_rv, line_rv;

	zend_call_known_instance_method_with_0_params(base_ce->__tostring, exception, &string);
	ZEND_ASSERT(Z_TYPE(string) == IS_STRING);

	const zval *file = zend_read_property_ex(base_ce, exception, ZSTR_KNOWN(ZEND_STR_FILE), true, &file_rv);
	const zval *line = zend_read_property_ex(base_ce, exception, ZSTR_KNOWN(ZEND_STR_LINE), true, &line_rv);

	zend_string *message = zend_strpprintf(0, "Uncaught %S\n  thrown", Z_STR(string));
	zend_string *file_name = Z_TYPE_P(file) == IS_STRING && Z_STRLEN_P(file) > 0 ? Z_STR_P(file) : NULL;
	const uint32_t line_number = Z_TYPE_P(line) == IS_LONG ? (uint32_t) Z_LVAL_P(line) : 0;

	/* A backtrace the fatal error left would be appended to this message, and zend_error() would fetch
	 * one of this call: zend_error_cb() is called directly, the stale one cleared, as
	 * zend_exception_error() does. */
	zval_ptr_dtor(&EG(last_fatal_error_backtrace));
	ZVAL_UNDEF(&EG(last_fatal_error_backtrace));
	zend_observer_error_notify(E_ERROR | E_DONT_BAIL, file_name, line_number, message);
	zend_error_cb(E_ERROR | E_DONT_BAIL, file_name, line_number, message);
	zend_string_release(message);
	zval_ptr_dtor(&string);
}

/* After the request's last drain, or at RSHUTDOWN after a bailout in the shutdown phase. A finished
 * coroutine still alive (in an array, a static property, a cycle) whose exception nobody observed adds
 * it to the table; the plain globals added theirs in coroutine_object_destroy. Each is printed as
 * uncaught. */
static void scheduler_print_unobserved_exceptions(void)
{
	const zend_objects_store *objects = &EG(objects_store);

	for (uint32_t i = 1; i < objects->top; i++) {
		zend_object *object = objects->object_buckets[i];

		if (EXPECTED(!IS_OBJ_VALID(object) || object->ce != async_ce_coroutine)) {
			continue;
		}

		async_coroutine_t *coroutine = async_coroutine_from_object(object);
		zend_object *exception = coroutine->coroutine.exception;

		if (EXPECTED(exception == NULL || !ZEND_COROUTINE_IS_FINISHED(&coroutine->coroutine) ||
					 (coroutine->coroutine.flags & ASYNC_COROUTINE_F_EXC_CAUGHT) ||
					 instanceof_function(exception->ce, async_ce_cancellation))) {
			continue;
		}

		coroutine->coroutine.flags |= ASYNC_COROUTINE_F_EXC_CAUGHT;
		GC_ADDREF(exception);
		async_unobserved_exception_add(exception);
	}

	/* main.c prints EG(exception) after this call. */
	zend_object *pending_exception = EG(exception);
	EG(exception) = NULL;

	/* Uncaught at the request's end, as main.c prints it: a throwing __toString() is not handed to
	 * set_exception_handler()'s handler. */
	zval user_exception_handler;
	ZVAL_COPY_VALUE(&user_exception_handler, &EG(user_exception_handler));
	ZVAL_UNDEF(&EG(user_exception_handler));

	/* A print runs PHP code, which may add to the table: each pass looks it up again. */
	while (true) {
		zend_object *exception = NULL;
		zval *value = NULL;

		ZEND_HASH_FOREACH_VAL(&ASYNC_G(unobserved_exceptions), value)
		{
			if (Z_TYPE_P(value) == IS_OBJECT) {
				exception = Z_OBJ_P(value);
				ZVAL_PTR(value, exception);
				break;
			}
		}
		ZEND_HASH_FOREACH_END();

		if (exception == NULL) {
			break;
		}

		/* After any bailout of the request, a fatal error's above all, the class's __toString() is not
		 * called: it would run unbounded after a timeout and may exhaust the memory limit again. Read per
		 * print: a print may bail out. CG(unclean_shutdown) cannot be cleared from PHP code, as
		 * error_clear_last() clears error_get_last(). */
		if (UNEXPECTED(CG(unclean_shutdown))) {
			zend_try
			{
				scheduler_print_exception_built_in(exception);
			}
			zend_end_try();
			continue;
		}

		/* The entry keeps its reference; zend_exception_error() releases this one. With no frame, a
		 * throwing __toString() bails out before that, as an exit() or a fatal error in it. */
		GC_ADDREF(exception);

		zend_try
		{
			zend_exception_error(exception, E_ERROR);
		}
		zend_catch
		{
			OBJ_RELEASE(exception);
		}
		zend_end_try();
	}

	ZVAL_COPY_VALUE(&EG(user_exception_handler), &user_exception_handler);
	EG(exception) = pending_exception;
}

/* The suspend slot's from_main calls (section 7): main finishes, the scheduler coroutine drains the
 * queue on its own stack and comes back here, a new main is minted on this stack. A bailout, the
 * call's or the drain's, is re-raised on the way out after the new main exists, so whatever runs
 * after the core's catch has a current coroutine. */
static bool scheduler_main_suspend(bool is_bailout)
{
	async_coroutine_t *main_coroutine = (async_coroutine_t *) ZEND_ASYNC_MAIN_COROUTINE;

	/* Main calls from_main running. A main still parked was left by a bailout raised on its stack during
	 * its suspend (a release in its pop, its tick) and caught by a zend_try of the core's that does not
	 * re-raise it (a shutdown function's, the destructors'): it may sit in the queue, and finishing it
	 * as a plain end would free it there. The call is that bailout's. */
	if (UNEXPECTED(main_coroutine != NULL && !ZEND_COROUTINE_IS_RUNNING(&main_coroutine->coroutine))) {
		is_bailout = true;
	}

	bool reraise_bailout = is_bailout;

	/* A bailout out of the previous call's main_coroutine_finish left no main to finish. */
	if (EXPECTED(main_coroutine != NULL)) {
		main_coroutine_finish(main_coroutine, is_bailout);
	}

	/* No scheduler: nothing was queued or deferred since the last one ended. */
	if (ASYNC_G(scheduler_coroutine) != NULL) {
		zend_fiber_context *scheduler_context = make_scheduler_current();
		const uint8_t flags = is_bailout ? ZEND_FIBER_TRANSFER_FLAG_BAILOUT : 0;

		reraise_bailout = (switch_to(scheduler_context, flags) & ZEND_FIBER_TRANSFER_FLAG_BAILOUT) != 0;
	}

	ZEND_ASSERT(circular_buffer_is_empty(&ASYNC_G(fiber_context_pool)));

	main_coroutine = main_coroutine_adopt();
	ZEND_ASYNC_MAIN_COROUTINE = &main_coroutine->coroutine;
	ZEND_ASYNC_CURRENT_COROUTINE = &main_coroutine->coroutine;

	/* The bailout's own error is what the request reports; the exit exception is dropped, as in
	 * TrueAsync. */
	if (UNEXPECTED(reraise_bailout)) {
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
		scheduler_exit_exception_printed(exit_exception);

		if (UNEXPECTED(EG(exception) != NULL)) {
			zend_exception_set_previous(EG(exception), exit_exception);
		} else {
			EG(exception) = exit_exception;
		}
	}

	/* Only the last call reaches this line in the shutdown, after the destructors. A bailout's call never
	 * gets here, a shutdown function's included (it re-raises above). */
	if (UNEXPECTED(EG(flags) & EG_FLAGS_IN_SHUTDOWN)) {
		scheduler_print_unobserved_exceptions();
	}

	return EG(exception) == NULL;
}

///////////////////////////////////////////////////////////////////
/// The slots
///////////////////////////////////////////////////////////////////

async_coroutine_t *async_coroutine_new(void)
{
	async_coroutine_t *coroutine = async_coroutine_from_object(async_ce_coroutine->create_object(async_ce_coroutine));

	zend_hash_index_add_new_ptr(&ASYNC_G(coroutines), coroutine->std.handle, coroutine);

	return coroutine;
}

static zend_coroutine_t *scheduler_new_coroutine(void)
{
	ASYNC_IO_PROVIDER_INSTALL_ONCE();

	return &async_coroutine_new()->coroutine;
}

/* The core's own coroutines (the collector's, the shutdown destructors') join the engine's scope, which
 * no user scope's cancel reaches (S9-scope.md section 3), and install no IO provider: a script that
 * only collects cycles keeps the core's blocking IO (dev/plans/S6.md section 2). A HI_PRIORITY
 * request, the collector's run, goes to the front of the queue on its first enqueue: its caller and
 * every coroutine that fills the root buffer before it runs wait for it with a parked stack. */
static zend_coroutine_t *scheduler_gc_new_coroutine(const zend_coroutine_priority priority)
{
	async_coroutine_t *coroutine = async_coroutine_new();

	async_scope_add_coroutine(ASYNC_G(engine_scope), coroutine);

	if (priority == ZEND_COROUTINE_HI_PRIORITY) {
		coroutine->coroutine.flags |= ASYNC_COROUTINE_F_HI_PRIORITY;
	}

	return &coroutine->coroutine;
}

static zend_coroutine_t *scheduler_launch(void)
{
	return &main_coroutine_adopt()->coroutine;
}

static zend_always_inline void run_queue_push(async_coroutine_t *coroutine)
{
	/* The front once after asHiPriority() or the core's HI_PRIORITY request (D20, D35). */
	if (UNEXPECTED(coroutine->coroutine.flags & ASYNC_COROUTINE_F_HI_PRIORITY)) {
		coroutine->coroutine.flags &= ~ASYNC_COROUTINE_F_HI_PRIORITY;
		circular_buffer_push_front(&ASYNC_G(run_queue), coroutine);
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

	/* Outside scheduler context the running current coroutine takes only the yield, its own enqueue
	 * with no error: a wake with an error is refused before the error is applied, as TrueAsync's
	 * async_coroutine_resume refuses a coroutine that is not suspended outside scheduler context
	 * (coroutine.c:804-808). Accepted, it would leave the coroutine in the queue after it finishes. In
	 * scheduler context the short path below takes it and pushes nothing. */
	if (UNEXPECTED(error != NULL && zend_coroutine == ZEND_ASYNC_CURRENT_COROUTINE &&
				   ZEND_COROUTINE_IS_RUNNING(zend_coroutine) && !ZEND_ASYNC_IN_SCHEDULER_CONTEXT)) {
		if (transfer_error) {
			OBJ_RELEASE(error);
		}

		zend_throw_error(NULL, "Cannot resume a coroutine that has not been suspended");
		return false;
	}

	if (UNEXPECTED(error != NULL)) {
		if (!transfer_error) {
			GC_ADDREF(error);
		}

		waker_apply_error(coroutine, error, false);
	}

	ASYNC_TEST_FAULT(ASYNC_TEST_FAULT_ENQUEUE);

	/* The current coroutine woken inside its own tick (U2): it is SUSPENDED or QUEUED there, or RUNNING
	 * after an earlier wake in the same tick, and a push by the status would switch into the running
	 * context later. */
	if (UNEXPECTED(zend_coroutine == ZEND_ASYNC_CURRENT_COROUTINE && ZEND_ASYNC_IN_SCHEDULER_CONTEXT)) {
		async_wait_unlink(coroutine);

		if (ZEND_COROUTINE_IS_SUSPENDED(zend_coroutine)) {
			ZEND_COROUTINE_SET_STATUS(zend_coroutine, ZEND_COROUTINE_STATUS_RUNNING);
		}

		return true;
	}

	switch (ZEND_COROUTINE_STATUS(zend_coroutine)) {
		case ZEND_COROUTINE_STATUS_CREATED:
			/* D11's point: a coroutine its creator placed in no scope joins the one spawn() would take; a
			 * Fiber's stays out, as in TrueAsync's fork (S9-scope.md section 3). */
			if (EXPECTED(coroutine->scope == NULL && !ZEND_COROUTINE_IS_FIBER(zend_coroutine))) {
				async_scope_add_coroutine(async_scope_current(), coroutine);
			}

			run_queue_push(coroutine);
			return true;
		case ZEND_COROUTINE_STATUS_SUSPENDED:
			run_queue_push(coroutine);
			async_wait_unlink(coroutine);
			return true;
		case ZEND_COROUTINE_STATUS_QUEUED:
			return true;
		case ZEND_COROUTINE_STATUS_RUNNING:
			/* Woken in its own pop and not current again yet (scheduler_suspend finishes the unrun
			 * coroutines popped with it): it runs on, and its suspend() throws the error as it returns,
			 * as TrueAsync accepts a wake after its short path (coroutine.c:845-852). */
			if (UNEXPECTED(zend_coroutine != ZEND_ASYNC_CURRENT_COROUTINE)) {
				return true;
			}

			/* The yield of Async\suspend(): the current coroutine goes to the back of the queue, and its
			 * caller suspends next (TrueAsync's Async\suspend, async.c:232-233). */
			run_queue_push(coroutine);
			return true;
		default:
			ZEND_UNREACHABLE();
			return false;
	}
}

bool async_coroutine_cancel(async_coroutine_t *coroutine,
							zend_object *error,
							const bool transfer_error,
							const bool is_safely)
{
	zend_coroutine_t *zend_coroutine = &coroutine->coroutine;

	ZEND_ASSERT(coroutine != ASYNC_G(scheduler_coroutine) && "the scheduler coroutine is never cancelled");

	if (UNEXPECTED(ZEND_COROUTINE_IS_FINISHED(zend_coroutine))) {
		if (error != NULL && transfer_error) {
			OBJ_RELEASE(error);
		}

		return true;
	}

#ifdef TRUE_ASYNC_TEST_HOOKS
	async_collector_check_cancel(coroutine);
#endif

	/* From here on the function owns one reference to the error. */
	if (EXPECTED(error == NULL)) {
		error = async_new_exception(async_ce_cancellation, "Coroutine cancelled");
	} else if (!transfer_error) {
		GC_ADDREF(error);
	}

	/* Inside protect() the first request waits for its end and the cancelled bit stays clear; tested
	 * before the running case, so a coroutine that cancels itself there defers too. */
	if (UNEXPECTED(zend_coroutine->flags & ASYNC_COROUTINE_F_PROTECTED)) {
		if (EXPECTED(coroutine->deferred_cancellation == NULL)) {
			coroutine->deferred_cancellation = error;
		} else {
			OBJ_RELEASE(error);
		}

		return true;
	}

	/* The running coroutine is not interrupted: the cancellation becomes its outcome, and what it
	 * throws later takes that as its previous. Inside its own suspend() it is SUSPENDED or QUEUED and
	 * takes the waker below. */
	if (UNEXPECTED(zend_coroutine == ZEND_ASYNC_CURRENT_COROUTINE && ZEND_COROUTINE_IS_RUNNING(zend_coroutine))) {
		ZEND_COROUTINE_SET_CANCELLED(zend_coroutine);

		/* D16's graceful exit comes from a dispatch, so only inside the coroutine's own suspend(),
		 * woken in its tick (U2): that suspend() throws it as it returns. As the outcome it would reach
		 * getException() and await(), which are not meant to see an exit object. */
		if (UNEXPECTED(async_is_exit_object(error))) {
			ZEND_ASSERT(ZEND_ASYNC_IN_SCHEDULER_CONTEXT);
			waker_apply_error(coroutine, error, true);
			return true;
		}

		if (EXPECTED(zend_coroutine->exception == NULL)) {
			zend_coroutine->exception = error;
		} else {
			OBJ_RELEASE(error);
		}

		return true;
	}

	/* Cancelled, as TrueAsync marks a zombie (coroutine.c:956-974), so isCancellationRequested() says
	 * so while it runs on. */
	ZEND_COROUTINE_SET_CANCELLED(zend_coroutine);

	if (UNEXPECTED(is_safely && ZEND_COROUTINE_IS_STARTED(zend_coroutine))) {
		async_scope_mark_zombie(coroutine);
		OBJ_RELEASE(error);
		return true;
	}

	waker_apply_error(coroutine, error, true);

	/* A coroutine that never ran is already queued (its spawn) and finishes unrun where it is popped;
	 * a parked one is queued here and the error is thrown inside its suspend(). */
	return async_scheduler_enqueue(zend_coroutine, NULL, false);
}

/* Cancels every unfinished coroutine with `cancellation`, or with AsyncCancellation("Graceful
 * shutdown") when it is NULL, protection cleared (TrueAsync's cancel_queued_coroutines,
 * scheduler.c:891-947). The current coroutine is cancelled too: it runs on, with the cancellation as
 * its outcome. In scheduler context, as TrueAsync's: a release on the way may start a collection,
 * which must not park the caller in the middle of the walk. */
static void scheduler_cancel_all(zend_object *cancellation)
{
	if (UNEXPECTED(cancellation != NULL)) {
		GC_ADDREF(cancellation);
	} else {
		cancellation = async_new_exception(async_ce_cancellation, "Graceful shutdown");
	}

	const bool was_in_scheduler_context = ZEND_ASYNC_IN_SCHEDULER_CONTEXT;
	ZEND_ASYNC_IN_SCHEDULER_CONTEXT = true;

	async_coroutine_t *coroutine = NULL;
	const uint32_t iterator = zend_hash_iterator_add(&ASYNC_G(coroutines), 0);
	uint32_t remaining = zend_hash_num_elements(&ASYNC_G(coroutines));

	while (remaining-- > 0 && (coroutine = registry_walk_next(iterator)) != NULL) {
		registry_cancel(coroutine, cancellation, false);
	}

	zend_hash_iterator_del(iterator);

	ZEND_ASYNC_IN_SCHEDULER_CONTEXT = was_in_scheduler_context;
	OBJ_RELEASE(cancellation);
}

void async_scheduler_graceful_shutdown(zend_object *cancellation)
{
	if (UNEXPECTED(ASYNC_G(graceful_shutdown))) {
		return;
	}

	ASYNC_G(graceful_shutdown) = true;
	scheduler_cancel_all(cancellation);
}

/* Submits D16's Timer for `ms`. One that cannot be submitted adds its Error to the request's exit
 * exception, and the shutdown goes on unbounded. A finished coroutine's handlers may have left an
 * exception, which its finish folds after the arm. */
static void exit_deadline_submit(async_io_event_t *event, const zend_long ms)
{
	php_io_op_timer(&event->op, php_io_deadline_from_ms(ms));

	zend_object *saved_exception = NULL;
	async_exception_save_fast(&EG(exception), &saved_exception);

	if (UNEXPECTED(async_reactor_submit_own(event) == FAILURE)) {
		zend_object *error = EG(exception);
		GC_ADDREF(error);
		zend_clear_exception();
		async_exit_exception_add(error);
	}

	async_exception_restore_fast(&EG(exception), &saved_exception);
}

/* D16: the coroutines the graceful shutdown left alive are unwound with the engine's graceful exit,
 * which no catch sees, protection cleared, so a protect() block does not hold the request either;
 * their finally blocks run. While any is left, the Timer fires again every ASYNC_EXIT_REFIRE_MS: a
 * finally that waits and a coroutine spawned since are unwound too, as TrueAsync's finally_shutdown
 * cancels again what was spawned (scheduler.c:1037-1065). In scheduler context: the reactor's
 * dispatch. */
static void
exit_deadline_fire(async_awaitable_t *target, async_event_callback_t *callback, void *result, zend_object *exception)
{
	(void) callback;
	(void) result;
	(void) exception;

	HashTable *coroutines = &ASYNC_G(coroutines);
	async_coroutine_t *coroutine = NULL;
	const uint32_t iterator = zend_hash_iterator_add(coroutines, 0);
	uint32_t remaining = zend_hash_num_elements(coroutines);

	while (remaining-- > 0 && (coroutine = registry_walk_next(iterator)) != NULL) {
		registry_cancel(coroutine, zend_create_graceful_exit(), true);
	}

	zend_hash_iterator_del(iterator);

	async_io_event_t *event = (async_io_event_t *) target;

	if (zend_hash_num_elements(coroutines) != 0) {
		event->base.flags &= ~ASYNC_EVENT_F_CLOSED;
		exit_deadline_submit(event, ASYNC_EXIT_REFIRE_MS);
		return;
	}

	/* Done: a later exit arms afresh. The dispatch's reference frees the event. */
	ASYNC_G(exit_deadline) = NULL;
	async_io_event_release(event);
}

/* D16 bounds the drain it was armed in: a shutdown function's own wait after it is not part of it. */
static void exit_deadline_withdraw(void)
{
	async_io_event_t *event = ASYNC_G(exit_deadline);

	if (UNEXPECTED(event != NULL)) {
		ASYNC_G(exit_deadline) = NULL;
		async_io_event_orphan(event);
		async_io_event_release(event);
	}
}

static async_event_callback_t exit_deadline_callback = {
	.flags = ASYNC_CALLBACK_F_SHARED,
	.callback = exit_deadline_fire,
};

/* Whether a coroutine is left that ran: one cancelled before it ran finishes where it is popped and
 * never waits. */
static bool registry_has_started(void)
{
	const async_coroutine_t *coroutine = NULL;

	ZEND_HASH_FOREACH_PTR(&ASYNC_G(coroutines), coroutine)
	{
		if (ZEND_COROUTINE_IS_STARTED(&coroutine->coroutine)) {
			return true;
		}
	}
	ZEND_HASH_FOREACH_END();

	return false;
}

/* D16 (S4.md 3.5): once per drain, when a coroutine that ran is left, a Timer on the reactor's own
 * list, so it keeps no coroutine from a deadlock. */
static void exit_deadline_arm(void)
{
	if (EXPECTED(ASYNC_G(exit_deadline) != NULL || !registry_has_started())) {
		return;
	}

	zend_long ms = ASYNC_EXIT_DEADLINE_MS;

#ifdef TRUE_ASYNC_TEST_HOOKS
	if (ASYNC_G(test_exit_deadline_ms) != 0) {
		ms = ASYNC_G(test_exit_deadline_ms);
	}
#endif

	async_io_event_t *event = async_io_event_new();
	async_callbacks_reserve(&event->base.callbacks, 1);
	async_callbacks_push_reserved(&event->base.callbacks, &exit_deadline_callback);
	ASYNC_G(exit_deadline) = event;
	exit_deadline_submit(event, ms);
}

void async_scheduler_cancel_for_exit(void)
{
	/* During the shutdown it cancels again what was spawned since (TrueAsync's finally_shutdown,
	 * scheduler.c:1037-1065). */
	if (UNEXPECTED(ASYNC_G(graceful_shutdown))) {
		scheduler_cancel_all(NULL);
	} else {
		async_scheduler_graceful_shutdown(NULL);
	}

	exit_deadline_arm();
}

void async_scheduler_exit_with(zend_object *exception)
{
	async_exit_exception_add(exception);
	async_scheduler_cancel_for_exit();
}

/* Parks the current coroutine (section 4.2). The tick runs on its stack, then it switches straight
 * to the next queued coroutine and returns when somebody switches back. A yield keeps the coroutine
 * QUEUED, anything else parks it SUSPENDED; woken in the tick, or first in the queue after its own
 * yield, it runs on with no switch. */
static bool scheduler_suspend(const bool from_main, const bool is_bailout)
{
	if (UNEXPECTED(from_main)) {
		return scheduler_main_suspend(is_bailout);
	}

	zend_coroutine_t *zend_coroutine = ZEND_ASYNC_CURRENT_COROUTINE;
	async_coroutine_t *coroutine = (async_coroutine_t *) zend_coroutine;

	if (UNEXPECTED(zend_coroutine == NULL)) {
		zend_throw_error(NULL, "There is no coroutine to suspend");
		return false;
	}

	/* Finalize releases what a finished coroutine held while it is still current: a destructor run
	 * there has no context to park (a Fiber's resume() or throw() reaches here). */
	if (UNEXPECTED(ZEND_COROUTINE_IS_FINISHED(zend_coroutine))) {
		async_wait_unlink(coroutine);
		zend_throw_error(NULL, "There is no coroutine to suspend");
		return false;
	}

	/* A park from the tick would leave the tick halfway (4.6). */
	if (UNEXPECTED(ZEND_ASYNC_IN_SCHEDULER_CONTEXT)) {
		async_wait_unlink(coroutine);
		zend_throw_error(NULL, "A coroutine cannot be stopped from the Scheduler context");
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
	zend_object **exception_ptr = &EG(exception);
	zend_object *saved_exception = NULL;
	async_exception_save_fast(exception_ptr, &saved_exception);

	/* An EH_THROW window this coroutine suspends in (an internal function's call into user code) is its
	 * own: the switch handlers, the tick and the pop below run other coroutines' code on this stack, and
	 * inside the window a warning of theirs would become this coroutine's exception class. The core's
	 * switch leaves the window behind the same way (zend_fibers.c, zend_fiber_switch_context). */
	zend_error_handling saved_error_handling;
	zend_replace_error_handling(EH_NORMAL, NULL, &saved_error_handling);

	/* An @ it suspends inside is its own too: the core keeps error_reporting per context
	 * (zend_fiber_vm_state), so the code below sees the INI value, as a new context does. Outside an @
	 * the two are equal already: error_reporting() and ini_set() write the ini entry. */
	const int saved_error_reporting = EG(error_reporting);

	if (UNEXPECTED(E_HAS_ONLY_FATAL_ERRORS(saved_error_reporting))) {
		EG(error_reporting) = (int) async_ini_error_reporting();
	}

	/* getTrace(), the suspend location and the GC read the parked frame from here. */
	coroutine->fiber_context->execute_data = EG(current_execute_data);

	if (!ZEND_COROUTINE_IS_QUEUED(zend_coroutine)) {
		ZEND_COROUTINE_SET_STATUS(zend_coroutine, ZEND_COROUTINE_STATUS_SUSPENDED);
	}

	/* Whoever watches the coroutine learns that it gives up the CPU before the tick, as in TrueAsync
	 * (scheduler.c:1708-1711): the core's shutdown destructors queue the coroutine that carries their
	 * pass on, and a destructor that only yields must let it run even with nobody else queued. */
	if (UNEXPECTED(coroutine->switch_handlers != NULL)) {
		async_switch_handlers_call(coroutine, false);
		ZEND_ASSERT(EG(exception) == NULL && "a switch handler throws nothing (TrueAsync's scheduler.c:1710)");
	}

	scheduler_tick(ASYNC_REACTOR_CHECK_INTERVAL);

	/* Whoever switches back here has made this coroutine current and RUNNING. */
	while (!ZEND_COROUTINE_IS_RUNNING(zend_coroutine)) {
		async_coroutine_t *next_coroutine = run_queue_pop();

		/* The pop finishes the coroutines cancelled before they ran, and what their release throws may
		 * start the graceful shutdown, which wakes this coroutine in its own tick (U2): it runs on, and
		 * the popped one keeps its turn. */
		if (UNEXPECTED(ZEND_COROUTINE_IS_RUNNING(zend_coroutine))) {
			if (next_coroutine != NULL) {
				circular_buffer_push_front(&ASYNC_G(run_queue), next_coroutine);
			}

			break;
		}

		/* Nothing queued: the scheduler coroutine waits for what comes next (TrueAsync's
		 * scheduler_next_tick, scheduler.c:1610-1613). */
		if (UNEXPECTED(next_coroutine == NULL)) {
			if (UNEXPECTED(switch_to(make_scheduler_current(), 0) & ZEND_FIBER_TRANSFER_FLAG_BAILOUT)) {
				async_wait_abort(coroutine);
				zend_bailout();
			}

			continue;
		}

		/* A yield with nobody ahead (B3), or one the fuzz hook picked first. */
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
				async_wait_abort(coroutine);
				zend_bailout();
			}
		}

		make_current(next_coroutine);

		/* The scheduler hands the bailout to a parked coroutine (scheduler_bailout_all) or to a parked
		 * main (its end): it is re-raised on this stack, which unwinds through its own try. */
		if (UNEXPECTED(switch_to(&next_coroutine->fiber_context->context, 0) & ZEND_FIBER_TRANSFER_FLAG_BAILOUT)) {
			async_wait_abort(coroutine);
			zend_bailout();
		}
	}

	if (UNEXPECTED(coroutine->switch_handlers != NULL)) {
		async_switch_handlers_call(coroutine, true);
	}

	async_wait_unlink(coroutine);

	/* Woken with an error (a cancellation): it is thrown here, and a result that came with it
	 * is dropped. */
	if (UNEXPECTED(coroutine->waker.error != NULL)) {
		zend_object *error = coroutine->waker.error;
		coroutine->waker.error = NULL;
		zval_ptr_dtor(&coroutine->waker.result);
		ZVAL_UNDEF(&coroutine->waker.result);
		waker_error_throw(error);
	}

	zend_restore_error_handling(&saved_error_handling);
	EG(error_reporting) = saved_error_reporting;
	async_exception_restore_fast(exception_ptr, &saved_exception);

	return *exception_ptr == NULL;
}

static bool scheduler_cancel(zend_coroutine_t *coroutine, zend_object *error, bool transfer_error, const bool is_safely)
{
	return async_coroutine_cancel((async_coroutine_t *) coroutine, error, error != NULL && transfer_error, is_safely);
}

/* Called by the core when exit() ends a fiber's body (zend_fiber_coroutine_entry() in
 * zend_fibers.c), with the exit still pending: the scheduler takes the exit over, as TrueAsync's
 * start_graceful_shutdown does, and the coroutines are cancelled as for exit() in a coroutine (D16).
 * With the exit cleared the core throws nothing to the fiber's caller and wakes it. */
static bool scheduler_shutdown(void)
{
	if (EXPECTED(EG(exception) != NULL && zend_is_unwind_exit(EG(exception)))) {
		zend_clear_exception();
	}

	async_scheduler_cancel_for_exit();

	return true;
}

/* The queue takes the caller's reference; the tick releases it. The scheduler coroutine runs the
 * tick when nothing else does (after main); on false the caller keeps its reference. */
static bool scheduler_defer(zend_async_microtask_t *microtask)
{
	if (UNEXPECTED(!scheduler_coroutine_ensure())) {
		return false;
	}

	circular_buffer_push_ptr_with_resize(&ASYNC_G(microtasks), microtask);

	return true;
}

/* Every fiber runs as a coroutine (S3.md section 8): a fiber left on the engine's own path would
 * switch stacks the scheduler does not know about. The core sets the entry point and the fiber bit
 * (zend_fibers.c, zend_fiber_adopt), as for the core's test_scheduler.c. */
static zend_coroutine_t *scheduler_intercept_fiber(zend_fiber *fiber)
{
	(void) fiber;

	/* A finished coroutine is still current while finalize releases what it held: the start would
	 * queue the body and then fail to park the caller, and the body would run later with nobody
	 * waiting. Refused before any coroutine exists; the core then leaves the Fiber unstarted. */
	const zend_coroutine_t *current_coroutine = ZEND_ASYNC_CURRENT_COROUTINE;

	if (UNEXPECTED(current_coroutine != NULL && ZEND_COROUTINE_IS_FINISHED(current_coroutine))) {
		/* The core's own refusal, whose class it keeps static (zend_fibers.c). */
		zend_class_entry *fiber_error = zend_hash_str_find_ptr(CG(class_table), ZEND_STRL("fibererror"));
		zend_throw_error(fiber_error, "Cannot switch fibers in current execution context");
		return NULL;
	}

	ASYNC_IO_PROVIDER_INSTALL_ONCE();

	return &async_coroutine_new()->coroutine;
}

/* The record's wake: its target finished, or the target's teardown fires a record that a throwing
 * callback left behind. The waiter reads the outcome from the target; the enqueue unlinks the
 * record (U1, U2). The wake marks nothing observed, unlike TrueAsync's
 * zend_async_waker_callback_resolve: the woken waiter may be cancelled before it reads the outcome.
 * Its frame holds the target, so the outcome is not the request's yet; await() marks it when it
 * reads it. */
static void
await_record_wake(async_awaitable_t *target, async_event_callback_t *callback, void *result, zend_object *exception)
{
	(void) target;
	(void) result;
	(void) exception;

	async_coroutine_event_callback_t *record = (async_coroutine_event_callback_t *) callback;

#ifdef TRUE_ASYNC_TEST_HOOKS
	async_collector_check_wake(record->coroutine, (const async_coroutine_t *) target);
#endif

	async_scheduler_enqueue(&record->coroutine->coroutine, NULL, false);
}

static zend_string *await_record_info(const async_coroutine_event_callback_t *record)
{
	return zend_strpprintf(0, "await: coroutine #%u", ((const async_coroutine_t *) record->event)->std.handle);
}

/* The waiter's frame holds the target: await()'s argument, or the reference the core's caller of the await
 * slot holds. */
static void await_record_collector_target(const async_coroutine_event_callback_t *record, async_collector_t *collector)
{
	async_collector_report_target(collector, &((async_coroutine_t *) record->event)->std, false);
}

/* A wait for a coroutine: its outcome is in the target, so nothing but the vector to leave. */
static const async_wait_kind_t async_wait_kind_coroutine = {
	.info = await_record_info,
	.collector_target = await_record_collector_target,
};

bool async_await_coroutine(async_coroutine_t *target, async_awaitable_t *token)
{
	async_coroutine_t *waiter = (async_coroutine_t *) ZEND_ASYNC_CURRENT_COROUTINE;

	ZEND_ASSERT(!ZEND_ASYNC_IN_SCHEDULER_CONTEXT && "the callers refuse a wait in scheduler context");

	if (UNEXPECTED(waiter == NULL)) {
		zend_throw_error(NULL, "await() requires a running coroutine");
		return false;
	}

	/* A finished target is read in place with no park, as TrueAsync replays a closed event first
	 * (async.c:327-340), so a waiter that cannot park may still do it; the outcome goes to the
	 * awaiter, not to the request's exit exception (async.c:318-320). */
	if (ZEND_COROUTINE_IS_FINISHED(&target->coroutine)) {
		target->coroutine.flags |= ASYNC_COROUTINE_F_EXC_CAUGHT;
		return true;
	}

	/* A finished coroutine is still current while finalize releases what it held (a destructor). */
	if (UNEXPECTED(ZEND_COROUTINE_IS_FINISHED(&waiter->coroutine))) {
		zend_throw_error(NULL, "await() requires a running coroutine");
		return false;
	}

	if (UNEXPECTED(waiter == target)) {
		zend_throw_error(NULL, "Cannot await a coroutine from within itself");
		return false;
	}

	/* A bailout that a shutdown function's zend_try caught can leave main's wait linked: it is ended
	 * first, as TrueAsync's ZEND_ASYNC_WAKER_NEW cleans a stale waker. */
	async_wait_end(waiter);

	/* Another enqueue than the target's finish (a foreign one) wakes the waiter early: it waits
	 * again, as the core's test_scheduler.c does. The outcome is marked observed only when the waiter
	 * gets it: one cancelled before the target finishes never sees it, and the target's exception
	 * then still ends the request. */
	while (!ZEND_COROUTINE_IS_FINISHED(&target->coroutine)) {
		if (token != NULL && UNEXPECTED(!async_await_token_check(token))) {
			return false;
		}

		ASYNC_TEST_FAULT(ASYNC_TEST_FAULT_RESERVE);
		async_callbacks_reserve(&target->callbacks, 1);

		if (token != NULL) {
			async_callbacks_reserve(async_awaitable_callbacks(token), 1);

			if (UNEXPECTED(!async_await_token_arm(token))) {
				return false;
			}
		}

		async_wait_link(&waiter->waker.records[0],
						waiter,
						(async_awaitable_t *) target,
						&async_wait_kind_coroutine,
						await_record_wake);

		if (token != NULL) {
			async_await_token_link(&waiter->waker.records[1], waiter, token);
		}

		ASYNC_TEST_FAULT(ASYNC_TEST_FAULT_LINK);

		if (UNEXPECTED(!ZEND_ASYNC_SUSPEND())) {
			return false;
		}
	}

	target->coroutine.flags |= ASYNC_COROUTINE_F_EXC_CAUGHT;

	return true;
}

/* The core's wait for a coroutine (the GC's, zend_gc.c). The scheduler's own work cannot wait:
 * false without an exception there, and the GC collects later. The caller holds a reference to the
 * target for the whole wait, as the slot asks. */
static bool scheduler_await(zend_coroutine_t *zend_coroutine)
{
	const async_coroutine_t *waiter = (async_coroutine_t *) ZEND_ASYNC_CURRENT_COROUTINE;

	if (UNEXPECTED(ZEND_ASYNC_IN_SCHEDULER_CONTEXT)) {
		return false;
	}

	/* A finished coroutine is still current while finalize releases what it held, and one that never
	 * ran finishes with no context (coroutine_finish_unrun): suspend() would refuse with an Error, and
	 * the GC collects later instead. */
	if (UNEXPECTED(waiter != NULL &&
				   (ZEND_COROUTINE_IS_FINISHED(&waiter->coroutine) || waiter->fiber_context == NULL))) {
		return false;
	}

	return async_await_coroutine((async_coroutine_t *) zend_coroutine, NULL);
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

static void awaiting_info_add(const async_coroutine_event_callback_t *record, void *arg)
{
	zval line;
	ZVAL_STR(&line, record->event_callback.kind->info(record));
	zend_hash_next_index_insert_new((zend_array *) arg, &line);
}

/* One line per linked record of the coroutine's wait (S3.md 4.7, S4.md 2.4), worded by its kind; NULL
 * when nothing is linked: a yield, a park of the core's, no wait. The add slot keeps nothing, so there
 * are no foreign lines: nothing in the core adds one. */
static zend_array *scheduler_get_awaiting_info(zend_coroutine_t *zend_coroutine)
{
	zend_array *info = zend_new_array(0);
	async_wait_walk((async_coroutine_t *) zend_coroutine, awaiting_info_add, info);

	if (zend_hash_num_elements(info) == 0) {
		zend_array_destroy(info);
		return NULL;
	}

	return info;
}

/* A coroutine leaves the registry when it finishes; the scheduler's own coroutine is never in it, and
 * a zombie is not counted (the slot's contract, zend_async_API.h). */
static uint32_t scheduler_get_coroutine_count(void)
{
	return zend_hash_num_elements(&ASYNC_G(coroutines)) - ASYNC_G(zombie_coroutines_count);
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
	if (UNEXPECTED(object->ce != async_ce_coroutine)) {
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

/* TrueAsync's async_asm_stack_call (scheduler.c:106-152): only the stack pointer moves to `newsp`,
 * and fn(arg) runs there as an ordinary C call, so the ABI keeps the callee-saved registers; not a
 * context switch (Go's asmcgocall). The guards: GCC 13 ignores `naked` on AArch64 and emits a
 * prologue the asm does not undo; the SysV variant is not the Windows x64 ABI; a ucontext core keeps
 * no stack pointer in the context's handle. */
#if defined(__aarch64__) && __has_attribute(naked) && !defined(ZEND_FIBER_UCONTEXT)
#define ASYNC_HAVE_STACK_SWITCH 1

__attribute__((naked)) static void async_asm_stack_call(void *newsp, void (*fn)(void *), void *arg)
{
	__asm__ volatile("mov  x9, sp\n\t"
					 "bic  x0, x0, #15\n\t"
					 "mov  sp, x0\n\t"
					 "stp  x30, x9, [sp, #-16]!\n\t"
					 "mov  x9, x1\n\t"
					 "mov  x0, x2\n\t"
					 "blr  x9\n\t"
					 "ldp  x30, x9, [sp], #16\n\t"
					 "mov  sp, x9\n\t"
					 "ret\n\t");
}
#elif defined(__x86_64__) && !defined(_WIN32) && __has_attribute(naked) && !defined(ZEND_FIBER_UCONTEXT)
#define ASYNC_HAVE_STACK_SWITCH 1

__attribute__((naked)) static void async_asm_stack_call(void *newsp, void (*fn)(void *), void *arg)
{
	/* The return address stays on the caller's stack; the second push keeps rsp 16-byte aligned at
	 * the call. */
	__asm__ volatile("movq %rsp, %rax\n\t"
					 "andq $-16, %rdi\n\t"
					 "movq %rdi, %rsp\n\t"
					 "pushq %rax\n\t"
					 "pushq %rax\n\t"
					 "movq %rsi, %r10\n\t"
					 "movq %rdx, %rdi\n\t"
					 "call *%r10\n\t"
					 "movq 8(%rsp), %rsp\n\t"
					 "ret\n\t");
}
#endif

/* TrueAsync's async_call_on_main_stack (scheduler.c:154-178): a foreign call (JNI, FFI) from a
 * coroutine runs on the OS thread stack, which runtimes such as ART check the stack pointer against.
 * With no current coroutine (before the launch, after the core turned async off) or in main, this is
 * the OS stack already, as in TrueAsync. Main finishes before the scheduler drains the queue, and the
 * OS stack then waits in the engine's context. Around that drain a coroutine other than main is
 * current while the OS stack still runs (the switch into the scheduler), so the running context
 * decides. The suspended OS stack is free below its handle (boost's saved registers), with a 256-byte
 * margin as in TrueAsync. fn must not re-enter PHP: PHP code there would run under the fiber's stack
 * limit and bailout, and a nested call would reuse the same spot. */
static void scheduler_call_on_main_stack(void (*fn)(void *), void *arg)
{
#ifdef ASYNC_HAVE_STACK_SWITCH
	const zend_coroutine_t *current_coroutine = ZEND_ASYNC_CURRENT_COROUTINE;

	if (current_coroutine != NULL && !ZEND_COROUTINE_IS_MAIN(current_coroutine)) {
		const async_coroutine_t *main_coroutine = (async_coroutine_t *) ZEND_ASYNC_MAIN_COROUTINE;
		const zend_fiber_context *os_stack_context =
				main_coroutine != NULL ? &main_coroutine->fiber_context->context : EG(main_fiber_context);

		if (EXPECTED(EG(current_fiber_context) != os_stack_context)) {
			async_asm_stack_call((char *) os_stack_context->handle - 256, fn, arg);
			return;
		}
	}
#endif

	fn(arg);
}

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
	.call_on_main_stack = scheduler_call_on_main_stack,
	.defer = scheduler_defer,
	.coroutine_from_object = scheduler_coroutine_from_object,
	.intercept_fiber = scheduler_intercept_fiber,
	.coroutine_execute_data = scheduler_coroutine_execute_data,
	.add_switch_handler = async_switch_handler_add,
	.remove_switch_handler = async_switch_handler_remove,
	.add_finish_handler = async_finish_handler_add,
	.remove_finish_handler = async_finish_handler_remove,
	.await = scheduler_await,
	.add_awaiting_info = scheduler_add_awaiting_info,
	.remove_awaiting_info = scheduler_remove_awaiting_info,
	.get_awaiting_info = scheduler_get_awaiting_info,
	.get_coroutine_count = scheduler_get_coroutine_count,
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
	circular_buffer_ctor(&ASYNC_G(run_queue));
	/* Grows as contexts park, up to ASYNC_FIBER_POOL_SIZE and the run queue's length. */
	circular_buffer_ctor(&ASYNC_G(fiber_context_pool));
	circular_buffer_ctor(&ASYNC_G(microtasks));
	zend_hash_init(&ASYNC_G(coroutines), 8, NULL, NULL, false);
	ASYNC_G(scheduler_coroutine) = NULL;
	ASYNC_G(interrupt_coroutine) = NULL;
	ASYNC_G(exit_deadline) = NULL;
	ASYNC_G(graceful_shutdown) = false;
	zend_hash_init(&ASYNC_G(unobserved_exceptions), 0, NULL, unobserved_exception_dtor, false);

#ifdef TRUE_ASYNC_FUZZ
	async_fuzz_init(&ASYNC_G(fuzz));
#endif

	/* The core turns async off at every request end (main.c). */
	ZEND_ASYNC_INITIALIZE;
}

/* Runs after the core turned async off (the main and current slots are NULL already). What is left
 * in the registry is the main coroutine minted by the last from_main call, a core coroutine whose
 * enqueue the scheduler refused, and, when a bailout cut that call short (U6), the coroutines it never
 * reached, a parked scheduler and its pool. Nothing may
 * run any more, so a parked stack is unmapped without unwinding: the heap never reclaims a fiber
 * stack, and a worker would lose one per such request. TrueAsync's dtor only releases the objects.
 * What the dropped frames held is leaked to the heap, which is silent after a bailout
 * (CG(unclean_shutdown)). Each coroutine finishes without handlers. The user values it drops, the
 * coroutine objects and the unobserved exceptions included, go to `released_values` for the caller to
 * release last: their destructors may bail out, which ends the caller's teardown there. */
void async_scheduler_request_shutdown(zend_array **released_values)
{
	/* A bailout in a shutdown destructor leaves no later from_main call to print in. What the last call
	 * printed is skipped. */
	if (UNEXPECTED(CG(unclean_shutdown))) {
		scheduler_print_unobserved_exceptions();
	}

	async_coroutine_t *coroutine = NULL;

	ZEND_HASH_FOREACH_PTR(&ASYNC_G(coroutines), coroutine)
	{
		async_fiber_context_t *fiber_context = coroutine->fiber_context;
		coroutine->fiber_context = NULL;

		/* No frame runs again: what a wait holds goes while its stack is still mapped (S9's channel
		 * waiter lives there, D29). */
		async_wait_abort(coroutine);

		/* Main's is a copy of the engine's context, and the OS stack behind it is not ours; NULL when a
		 * bailout came out of main's own finish. */
		if (ZEND_COROUTINE_IS_MAIN(&coroutine->coroutine)) {
			if (EXPECTED(fiber_context != NULL)) {
				EG(current_fiber_context) = EG(main_fiber_context);
				efree(fiber_context);
			}
		} else if (UNEXPECTED(fiber_context != NULL)) {
			zend_fiber_destroy_context(&fiber_context->context);
		}

		async_wait_end(coroutine);
		async_switch_handlers_free(coroutine);
		ZEND_COROUTINE_SET_STATUS(&coroutine->coroutine, ZEND_COROUTINE_STATUS_FINISHED);
	}
	ZEND_HASH_FOREACH_END();

	/* Out of their scopes first: a coroutine is freed out of any scope. */
	async_scope_request_shutdown(released_values);

	if (*released_values == NULL) {
		*released_values = zend_new_array(zend_hash_num_elements(&ASYNC_G(coroutines)));
	}

	/* Released after every wait is unlinked: a target freed with a waiter linked would wake it, which
	 * creates a scheduler. */
	ZEND_HASH_FOREACH_PTR(&ASYNC_G(coroutines), coroutine)
	{
		zval coroutine_value;

		ZVAL_OBJ(&coroutine_value, &coroutine->std);
		zend_hash_next_index_insert_new(*released_values, &coroutine_value);
	}
	ZEND_HASH_FOREACH_END();

	zend_hash_destroy(&ASYNC_G(coroutines));

	/* A scheduler never entered (created after the last from_main call that entered one) or parked by
	 * that bailout. */
	async_coroutine_t *scheduler_coroutine = ASYNC_G(scheduler_coroutine);

	if (UNEXPECTED(scheduler_coroutine != NULL)) {
		ASYNC_G(scheduler_coroutine) = NULL;
		zend_fiber_destroy_context(&scheduler_coroutine->fiber_context->context);
		scheduler_coroutine->fiber_context = NULL;
		OBJ_RELEASE(&scheduler_coroutine->std);
	}

	async_fiber_context_t *fiber_context = NULL;

	while (circular_buffer_pop_ptr(&ASYNC_G(fiber_context_pool), (void **) &fiber_context) == SUCCESS) {
		zend_fiber_destroy_context(&fiber_context->context);
	}

	circular_buffer_dtor(&ASYNC_G(run_queue));
	circular_buffer_dtor(&ASYNC_G(fiber_context_pool));

	/* A microtask that never got its tick is released unrun, as a cancelled one (test_scheduler.c). */
	zend_async_microtask_t *microtask = NULL;

	while (circular_buffer_pop_ptr(&ASYNC_G(microtasks), (void **) &microtask) == SUCCESS) {
		ZEND_ASYNC_MICROTASK_RELEASE(microtask);
	}

	circular_buffer_dtor(&ASYNC_G(microtasks));

	/* The printed exceptions and what a bailout left unprinted. */
	zval *exception_value = NULL;

	ZEND_HASH_FOREACH_VAL(&ASYNC_G(unobserved_exceptions), exception_value)
	{
		zval exception;

		ZVAL_OBJ(&exception, Z_TYPE_P(exception_value) == IS_PTR ? Z_PTR_P(exception_value) : Z_OBJ_P(exception_value));
		zend_hash_next_index_insert_new(*released_values, &exception);
	}
	ZEND_HASH_FOREACH_END();

	ASYNC_G(unobserved_exceptions).pDestructor = NULL;
	zend_hash_destroy(&ASYNC_G(unobserved_exceptions));

	/* Before the reactor destroys the queue: a bailout cut the drain short. */
	exit_deadline_withdraw();

	if (UNEXPECTED(ZEND_ASYNC_EXIT_EXCEPTION != NULL)) {
		OBJ_RELEASE(ZEND_ASYNC_EXIT_EXCEPTION);
		ZEND_ASYNC_EXIT_EXCEPTION = NULL;
	}
}
