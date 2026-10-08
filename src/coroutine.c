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
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "php.h"
#include "php_true_async.h"
#include "Zend/zend_builtin_functions.h"
#include "Zend/zend_closures.h"
#include "coroutine.h"
#include "exceptions.h"
#include "iterator.h"
#include "scheduler.h"
#include "scope.h"
#include "coroutine_arginfo.h"

zend_class_entry *async_ce_coroutine = NULL;

static zend_object_handlers coroutine_handlers;

static zend_object *coroutine_object_create(zend_class_entry *class_entry)
{
	/* 472 B: a class without properties takes the inline properties slot off the size. */
	async_coroutine_t *coroutine = zend_object_alloc(sizeof(async_coroutine_t), class_entry);

	ZVAL_UNDEF(&coroutine->coroutine.result);
	ZVAL_UNDEF(&coroutine->waker.result);
	coroutine->coroutine.object_offset = offsetof(async_coroutine_t, std);
	zend_async_internal_context_init(&coroutine->coroutine);

	zend_object_std_init(&coroutine->std, class_entry);
	object_properties_init(&coroutine->std, class_entry);

	return &coroutine->std;
}

/* Drops what spawn()'s cache owns (Async_spawn): the object, the closure and a __call trampoline
 * copy, and leaves the cache empty, so a second call does nothing. zend_fcc_dtor would assert a
 * handler, and a run consumed the trampoline and cleared it. */
static void spawn_fcall_cache_release(zend_fcall_t *fcall)
{
	zend_fcall_info_cache cache = fcall->fci_cache;
	fcall->fci_cache = empty_fcall_info_cache;
	zend_release_fcall_info_cache(&cache);

	if (cache.object != NULL) {
		OBJ_RELEASE(cache.object);
	}

	if (cache.closure != NULL) {
		OBJ_RELEASE(cache.closure);
	}
}

/* Releases the values a coroutine holds that can run PHP code when they go: its arguments, result,
 * outcome, contexts, wait state and finally handlers. Each field is cleared before its release, so a destructor that
 * the release runs finds the coroutine without it. Returns the outcome exception, still referenced,
 * for the caller to release or throw. */
static zend_object *coroutine_release_values(async_coroutine_t *coroutine)
{
	zend_coroutine_t *zend_coroutine = &coroutine->coroutine;
	zend_fcall_t *fcall = zend_coroutine->fcall;

	if (EXPECTED(fcall == &coroutine->spawn_fcall)) {
		/* ZEND_ASYNC_FCALL_FREE without the free: the block is part of the coroutine. */
		zend_coroutine->fcall = NULL;
		zend_fcall_info_args_clear(&fcall->fci, true);

		if (UNEXPECTED(fcall->fci.named_params != NULL)) {
			zend_array_release(fcall->fci.named_params);
		}

		zval_ptr_dtor(&fcall->fci.function_name);

		spawn_fcall_cache_release(fcall);
	} else if (UNEXPECTED(fcall != NULL)) {
		zend_coroutine->fcall = NULL;
		ZEND_ASYNC_FCALL_FREE(fcall);
	}

	zval value;

	ZVAL_COPY_VALUE(&value, &zend_coroutine->result);
	ZVAL_UNDEF(&zend_coroutine->result);
	zval_ptr_dtor(&value);

	ZVAL_COPY_VALUE(&value, &coroutine->waker.result);
	ZVAL_UNDEF(&coroutine->waker.result);
	zval_ptr_dtor(&value);

	zend_object *deferred_cancellation = coroutine->deferred_cancellation;

	if (UNEXPECTED(deferred_cancellation != NULL)) {
		coroutine->deferred_cancellation = NULL;
		OBJ_RELEASE(deferred_cancellation);
	}

	zend_object *pending_error = coroutine->waker.error;

	if (UNEXPECTED(pending_error != NULL)) {
		coroutine->waker.error = NULL;
		OBJ_RELEASE(pending_error);
	}

	zend_object *context = zend_coroutine->context;

	if (UNEXPECTED(context != NULL)) {
		zend_coroutine->context = NULL;
		OBJ_RELEASE(context);
	}

	zend_hash_clean(&zend_coroutine->internal_context);

	HashTable *finally_handlers = coroutine->finally_handlers;

	if (UNEXPECTED(finally_handlers != NULL)) {
		coroutine->finally_handlers = NULL;
		zend_array_release(finally_handlers);
	}

	zend_object *exception = zend_coroutine->exception;
	zend_coroutine->exception = NULL;

	return exception;
}

/* The values go here, at the last reference, not in free_obj, as in TrueAsync's
 * coroutine_object_destroy (coroutine.c:164-242): a destructor that their release runs may take the
 * object again (Async\current_coroutine() while the finished coroutine is still current), and the
 * engine keeps an object taken again after dtor_obj but frees the block after free_obj whatever
 * its refcount (zend_objects_store_del). The store's destructor pass at shutdown skips coroutine
 * objects (zend_objects_store_call_destructors_async), so a queued one keeps its arguments until it
 * runs. An exception nobody observed is thrown where the last reference went
 * (TrueAsync's coroutine.c:219-230), unless it is a cancellation. Where no PHP code runs (the
 * shutdown's destructors release the globals) it is printed at the request's end, which TrueAsync
 * does not do (DECISIONS 2026-10-05). */
static void coroutine_object_destroy(zend_object *object)
{
	async_coroutine_t *coroutine = async_coroutine_from_object(object);
	zend_object *exception = coroutine_release_values(coroutine);

	if (EXPECTED(exception == NULL)) {
		return;
	}

	if ((coroutine->coroutine.flags & ASYNC_COROUTINE_F_EXC_CAUGHT) ||
		instanceof_function(exception->ce, async_ce_cancellation)) {
		OBJ_RELEASE(exception);
		return;
	}

	if (EXPECTED(EG(current_execute_data) != NULL)) {
		zend_throw_exception_internal(exception);
		return;
	}

	/* Async is off only after the request's last print. */
	if (EXPECTED(ZEND_ASYNC_IS_ACTIVE)) {
		async_unobserved_exception_add(exception);
		return;
	}

	OBJ_RELEASE(exception);
}

static void coroutine_object_free(zend_object *object)
{
	async_coroutine_t *coroutine = async_coroutine_from_object(object);

	/* The steps that fill these fields release them before the object dies. */
	ZEND_ASSERT(coroutine->fiber_context == NULL);
	ZEND_ASSERT(coroutine->scope == NULL);
	ZEND_ASSERT(coroutine->switch_handlers == NULL);

	/* Whoever attached itself to this coroutine (a fiber, say) lets go of it first. Here and not in
	 * finalize, as in the core's test_scheduler.c: a fiber holds a reference to its coroutine and
	 * releases it when the Fiber goes (zend_fibers.c, zend_fiber_release_coroutine); its dispose only
	 * forgets the coroutine, so a call at the finish would leak that reference. */
	if (UNEXPECTED(coroutine->coroutine.extended_dispose != NULL)) {
		coroutine->coroutine.extended_dispose(&coroutine->coroutine);
	}

	async_callbacks_free((async_awaitable_t *) coroutine, &coroutine->callbacks);

	zend_object *exception = coroutine_release_values(coroutine);

	if (UNEXPECTED(exception != NULL)) {
		OBJ_RELEASE(exception);
	}

	if (coroutine->coroutine.filename != NULL) {
		zend_string_release_ex(coroutine->coroutine.filename, false);
	}

	zend_async_internal_context_destroy(&coroutine->coroutine);
	zend_object_std_dtor(object);
}

/* A coroutine comes only from spawn: one built by `new` would have no entry point. */
static ZEND_COLD zend_function *coroutine_object_get_constructor(zend_object *object)
{
	zend_throw_error(NULL, "Instantiation of class Async\\Coroutine is not allowed, use Async\\spawn()");

	return NULL;
}

static HashTable *coroutine_object_gc(zend_object *object, zval **table, int *num)
{
	async_coroutine_t *coroutine = async_coroutine_from_object(object);
	zend_get_gc_buffer *gc_buffer = zend_get_gc_buffer_create();

	zend_get_gc_buffer_add_zval(gc_buffer, &coroutine->coroutine.result);
	zend_get_gc_buffer_add_zval(gc_buffer, &coroutine->waker.result);

	/* An exception's trace can point back at this coroutine; without these edges such a cycle
	 * never collects. */
	if (UNEXPECTED(coroutine->coroutine.exception != NULL)) {
		zend_get_gc_buffer_add_obj(gc_buffer, coroutine->coroutine.exception);
	}

	if (UNEXPECTED(coroutine->deferred_cancellation != NULL)) {
		zend_get_gc_buffer_add_obj(gc_buffer, coroutine->deferred_cancellation);
	}

	if (UNEXPECTED(coroutine->waker.error != NULL)) {
		zend_get_gc_buffer_add_obj(gc_buffer, coroutine->waker.error);
	}

	const zend_fcall_t *fcall = coroutine->coroutine.fcall;

	if (fcall != NULL) {
		zend_get_gc_buffer_add_zval(gc_buffer, (zval *) &fcall->fci.function_name);

		for (uint32_t i = 0; i < fcall->fci.param_count; i++) {
			zend_get_gc_buffer_add_zval(gc_buffer, &fcall->fci.params[i]);
		}

		if (UNEXPECTED(fcall->fci.named_params != NULL)) {
			zend_get_gc_buffer_add_ht(gc_buffer, fcall->fci.named_params);
		}

		/* spawn()'s cache holds its references (Async_spawn). The core's fcall, a Fiber's, holds the
		 * object its callable resolved to (ZEND_ASYNC_FCALL_DEFINE). */
		if (EXPECTED(fcall == &coroutine->spawn_fcall)) {
			if (fcall->fci_cache.object != NULL) {
				zend_get_gc_buffer_add_obj(gc_buffer, fcall->fci_cache.object);
			}

			if (fcall->fci_cache.closure != NULL) {
				zend_get_gc_buffer_add_obj(gc_buffer, fcall->fci_cache.closure);
			}
		} else if (fcall->fci.object != NULL) {
			zend_get_gc_buffer_add_obj(gc_buffer, fcall->fci.object);
		}
	}

	if (UNEXPECTED(coroutine->coroutine.context != NULL)) {
		zend_get_gc_buffer_add_obj(gc_buffer, coroutine->coroutine.context);
	}

	if (UNEXPECTED(coroutine->finally_handlers != NULL)) {
		zend_get_gc_buffer_add_ht(gc_buffer, coroutine->finally_handlers);
	}

	/* The table is a field of this block, not a refcounted array: the collector gets its values, never
	 * the table itself, or it would free the table out of the middle of the coroutine. */
	zval *value;

	ZEND_HASH_FOREACH_VAL(&coroutine->coroutine.internal_context, value)
	{
		zend_get_gc_buffer_add_zval(gc_buffer, value);
	}
	ZEND_HASH_FOREACH_END();

	/* The parked stack is not walked: trial deletion keeps whatever its frames hold, since it
	 * cannot explain those references. */
	zend_get_gc_buffer_use(gc_buffer, table, num);

	return NULL;
}

///////////////////////////////////////////////////////////////////
/// Running and finishing
///////////////////////////////////////////////////////////////////

/* Takes the reference. The waiters of one failed coroutine rethrow one object: it is printed once. */
void async_unobserved_exception_add(zend_object *exception)
{
	zval value;

	ZVAL_OBJ(&value, exception);

	if (UNEXPECTED(zend_hash_index_add(&ASYNC_G(unobserved_exceptions), exception->handle, &value) == NULL)) {
		OBJ_RELEASE(exception);
	}
}

void async_exit_exception_add(zend_object *exception)
{
	if (UNEXPECTED(ZEND_ASYNC_EXIT_EXCEPTION != NULL)) {
		zend_exception_set_previous(exception, ZEND_ASYNC_EXIT_EXCEPTION);
	}

	ZEND_ASYNC_EXIT_EXCEPTION = exception;
}

/* The exception pending in EG becomes the coroutine's outcome. Over an outcome already stored (a
 * cancellation of the running coroutine, which ran on) it keeps that one as its previous, unless it
 * is itself a cancellation. An exit unwinds the coroutine and is no outcome; true when it was
 * exit(), which ends the request (D16). */
static bool coroutine_take_exception(async_coroutine_t *coroutine)
{
	zend_object *exception = EG(exception);

	GC_ADDREF(exception);
	zend_clear_exception();

	if (UNEXPECTED(zend_is_graceful_exit(exception) || zend_is_unwind_exit(exception))) {
		const bool is_exit = zend_is_unwind_exit(exception);
		OBJ_RELEASE(exception);
		return is_exit;
	}

	zend_object *outcome = coroutine->coroutine.exception;

	if (UNEXPECTED(outcome != NULL && instanceof_function(exception->ce, async_ce_cancellation))) {
		OBJ_RELEASE(exception);
		return false;
	}

	if (UNEXPECTED(outcome != NULL)) {
		zend_exception_set_previous(exception, outcome);
	}

	coroutine->coroutine.exception = exception;

	return false;
}

void async_coroutine_execute(async_coroutine_t *coroutine)
{
	zend_coroutine_t *zend_coroutine = &coroutine->coroutine;

	ZEND_ASSERT(zend_coroutine == ZEND_ASYNC_CURRENT_COROUTINE && ZEND_COROUTINE_IS_RUNNING(zend_coroutine));
	/* A fiber's coroutine has both: its entry point calls the fiber's function through the fcall. */
	ZEND_ASSERT(zend_coroutine->fcall != NULL || zend_coroutine->internal_entry != NULL);

	ZEND_COROUTINE_SET_STARTED(zend_coroutine);

	zend_try
	{
		if (UNEXPECTED(zend_coroutine->internal_entry != NULL)) {
			zend_coroutine->internal_entry();
		} else {
			zend_coroutine->fcall->fci.retval = &zend_coroutine->result;
			zend_call_function(&zend_coroutine->fcall->fci, &zend_coroutine->fcall->fci_cache);
			zend_coroutine->fcall->fci.retval = NULL;

			/* The callable goes with the run, as in TrueAsync: a finished coroutine holds no
			 * closure. Unset before the release, which runs destructors that may bail out; what
			 * they throw is the coroutine's outcome. */
			zval function_name;
			ZVAL_COPY_VALUE(&function_name, &zend_coroutine->fcall->fci.function_name);
			ZVAL_UNDEF(&zend_coroutine->fcall->fci.function_name);
			zval_ptr_dtor(&function_name);

			if (EXPECTED(zend_coroutine->fcall == &coroutine->spawn_fcall)) {
				spawn_fcall_cache_release(zend_coroutine->fcall);
			}
		}
	}
	zend_catch
	{
		zend_coroutine->flags |= ASYNC_COROUTINE_F_BAILOUT;
	}
	zend_end_try();

	/* Read before finalize, which may free the coroutine. */
	const bool is_bailout = (zend_coroutine->flags & ASYNC_COROUTINE_F_BAILOUT) != 0;

	async_coroutine_finalize(coroutine);

	/* Finished and maybe freed: not current for the tick that follows (the caller makes the scheduler
	 * current), nor for the bailout's drop (TrueAsync, coroutine.c:567). */
	ZEND_ASYNC_CURRENT_COROUTINE = NULL;

	if (UNEXPECTED(is_bailout)) {
		zend_bailout();
	}
}

void async_coroutine_finalize(async_coroutine_t *coroutine)
{
	zend_coroutine_t *zend_coroutine = &coroutine->coroutine;
	const bool is_bailout = (zend_coroutine->flags & ASYNC_COROUTINE_F_BAILOUT) != 0;

	/* Linked only when a bailout unwound the waiting frame, main's included when a shutdown function's
	 * zend_try caught it (TrueAsync's finalize destroys the waker the same way): that frame never runs
	 * again, so the wait is aborted, and its block released. */
	if (UNEXPECTED(!async_wait_is_empty(coroutine))) {
		async_wait_abort(coroutine);
		async_wait_end(coroutine);
	}

	bool is_exit = false;

	if (UNEXPECTED(EG(exception) != NULL)) {
		is_exit = coroutine_take_exception(coroutine);
	}

	ZEND_COROUTINE_SET_STATUS(zend_coroutine, ZEND_COROUTINE_STATUS_FINISHED);

	/* Out of the registry before any handler runs: a bailout out of the notify would leave a finished
	 * coroutine there, and a later drain in the request would count it as a waiter forever. TrueAsync
	 * deletes it after its catch (coroutine.c:747-767). */
	zend_hash_index_del(&ASYNC_G(coroutines), coroutine->std.handle);

	/* Only a started coroutine becomes a zombie; the other places that drop a registry entry drop
	 * coroutines that never ran. */
	if (UNEXPECTED(zend_coroutine->flags & ASYNC_COROUTINE_F_ZOMBIE)) {
		ASYNC_G(zombie_coroutines_count)--;
	}

	/* exit() ends the request gracefully, as in TrueAsync (D16): the other coroutines are cancelled,
	 * this one no longer, being finished. */
	if (UNEXPECTED(is_exit)) {
		async_scheduler_cancel_for_exit();
	}

	/* The context stays with the loop that ran the body; main's copy was freed by main_coroutine_finish. */
	coroutine->fiber_context = NULL;

	/* A finished coroutine switches no more: its handlers go, as in TrueAsync (coroutine.c:624). */
	async_switch_handlers_free(coroutine);

	/* The notify may drop every other reference to the object, and a finish handler may clear the
	 * exception (the finish handler contract, Zend/zend_async_API.h): both live until the end of this function. */
	zend_object *exception = zend_coroutine->exception;

	GC_ADDREF(&coroutine->std);

	if (UNEXPECTED(exception != NULL)) {
		GC_ADDREF(exception);
	}

	/* Set by the records of await_* (await.c, await_mark_handled), as TrueAsync's callbacks
	 * (async_API.c:390, 487); the record of await() marks nothing (scheduler.c, await_record_wake). */
	zend_coroutine->flags &= ~ASYNC_COROUTINE_F_EXCEPTION_HANDLED;
	const bool is_waiter_woken = async_callbacks_notify(
			(async_awaitable_t *) coroutine, &coroutine->callbacks, &zend_coroutine->result, exception);

	/* A finish handler may give the coroutine another outcome: an iterator worker that never ran ends with
	 * the walk's error (iterator.c). The records that read the notify's argument marked the old one. */
	if (UNEXPECTED(zend_coroutine->exception != NULL && zend_coroutine->exception != exception)) {
		if (exception != NULL) {
			OBJ_RELEASE(exception);
		}

		exception = zend_coroutine->exception;
		GC_ADDREF(exception);
		zend_coroutine->flags &= ~ASYNC_COROUTINE_F_EXCEPTION_HANDLED;
	}

	/* Observed: a waiter took the exception, or a finish handler cleared it. */
	if (exception != NULL &&
		((zend_coroutine->flags & ASYNC_COROUTINE_F_EXCEPTION_HANDLED) || zend_coroutine->exception == NULL)) {
		zend_coroutine->flags |= ASYNC_COROUTINE_F_EXC_CAUGHT;
	}

	async_callbacks_free((async_awaitable_t *) coroutine, &coroutine->callbacks);

	/* An error no waiter was parked for and nothing observed goes through the scope (S9-scope.md 4 and
	 * 9, item 6); a cancellation ends here, and after a bailout nothing more runs. A handler that takes it
	 * observes it. What the waiters threw is left for the request's end below. */
	if (UNEXPECTED(exception != NULL && !is_bailout && !is_waiter_woken && coroutine->scope != NULL &&
				   !(zend_coroutine->flags & ASYNC_COROUTINE_F_EXC_CAUGHT) &&
				   !instanceof_function(exception->ce, async_ce_cancellation) && EG(exception) == NULL)) {
		if (async_scope_catch(coroutine, exception)) {
			zend_coroutine->flags |= ASYNC_COROUTINE_F_EXC_CAUGHT;
		}
	}

	/* Nobody can observe the exception when only the scheduler's reference and this function's are
	 * left, or for main and a fiber, which nobody awaits through the object: it ends the request. A
	 * cancellation is the scheduler's own doing; after a bailout the request ends anyway. */
	if (UNEXPECTED(exception != NULL && !is_bailout && !(zend_coroutine->flags & ASYNC_COROUTINE_F_EXC_CAUGHT) &&
				   !instanceof_function(exception->ce, async_ce_cancellation) &&
				   (GC_REFCOUNT(&coroutine->std) <= 2 || ZEND_COROUTINE_IS_MAIN(zend_coroutine) ||
					ZEND_COROUTINE_IS_FIBER(zend_coroutine)))) {
		zend_coroutine->flags |= ASYNC_COROUTINE_F_EXC_CAUGHT;
		GC_ADDREF(exception);
		async_scheduler_exit_with(exception);
	}

	/* After the route and the unheld check, unlike TrueAsync, which starts them first (coroutine.c:680), so
	 * that the cancel either makes does not cancel them unrun (S9-scope.md 9, item 18). Before the
	 * coroutine leaves its scope: the run's scope is a child of it and keeps it from being disposed.
	 * Destroyed unrun after a bailout, as TrueAsync's (coroutine.c:1334-1340). */
	HashTable *finally_handlers = coroutine->finally_handlers;

	if (UNEXPECTED(finally_handlers != NULL)) {
		coroutine->finally_handlers = NULL;

		if (EXPECTED(!is_bailout)) {
			async_finally_handlers_start(finally_handlers, coroutine->scope, &coroutine->std);
		} else {
			zend_array_release(finally_handlers);
		}
	}

	/* After its waiters, as TrueAsync's (coroutine.c:727-731): the scope may be disposed with it. */
	if (EXPECTED(coroutine->scope != NULL)) {
		async_scope_remove_coroutine(coroutine);
	}

	/* What the waiters and finish handlers threw ends the request too. */
	if (UNEXPECTED(EG(exception) != NULL)) {
		zend_object *handler_exception = EG(exception);
		GC_ADDREF(handler_exception);
		zend_clear_exception();
		async_scheduler_exit_with(handler_exception);
	}

	if (UNEXPECTED(exception != NULL)) {
		OBJ_RELEASE(exception);
	}

	OBJ_RELEASE(&coroutine->std);
	/* The scheduler's birth reference. */
	OBJ_RELEASE(&coroutine->std);
}

///////////////////////////////////////////////////////////////////
/// Finally handlers
///////////////////////////////////////////////////////////////////

/* A run of the finally handlers of a scope or a coroutine on the iterator core (S9-scope.md 7). */
typedef struct
{
	async_iterator_t iterator;
	zend_object *target; /* the handlers' argument, held; NULL passes null */
} finally_run_t;

/* Calls one handler; its error goes to the iterator's exception, which the run's last worker ends with,
 * so the next handlers run too: the first alone, then a CompositeException of all (TrueAsync's
 * finally_handlers_iterator_handler, coroutine.c:1168-1222). An exit stops the run. */
static zend_result finally_handler_call(async_iterator_t *iterator, zval *handler, zval *key)
{
	(void) key;

	finally_run_t *run = (finally_run_t *) iterator;
	zval argument;
	zval retval;

	if (EXPECTED(run->target != NULL)) {
		ZVAL_OBJ(&argument, run->target);
	} else {
		ZVAL_NULL(&argument);
	}

	call_user_function(NULL, NULL, handler, &retval, 1, &argument);
	zval_ptr_dtor(&retval);

	zend_object *error = EG(exception);

	if (EXPECTED(error == NULL) || UNEXPECTED(async_is_exit_object(error))) {
		return SUCCESS;
	}

	GC_ADDREF(error);
	zend_clear_exception();

	if (iterator->exception == NULL) {
		iterator->exception = error;
		return SUCCESS;
	}

	if (iterator->exception->ce != async_ce_composite_exception) {
		zend_object *composite = async_new_exception(async_ce_composite_exception, "");

		async_composite_exception_add_exception(composite, iterator->exception);
		OBJ_RELEASE(iterator->exception);
		iterator->exception = composite;
	}

	async_composite_exception_add_exception(iterator->exception, error);
	OBJ_RELEASE(error);

	return SUCCESS;
}

static void finally_run_dtor(async_iterator_t *iterator)
{
	finally_run_t *run = (finally_run_t *) iterator;

	if (run->target != NULL) {
		zend_object *target = run->target;
		run->target = NULL;
		OBJ_RELEASE(target);
	}
}

bool async_finally_handlers_start(HashTable *finally_handlers, async_scope_t *scope, zend_object *target)
{
	if (UNEXPECTED(!ZEND_ASYNC_IS_ACTIVE)) {
		zend_array_release(finally_handlers);
		return false;
	}

	zval handlers;

	ZVAL_ARR(&handlers, finally_handlers);

	finally_run_t *run = (finally_run_t *) async_iterator_new(
			&handlers, NULL, NULL, finally_handler_call, async_scope_new(scope), 0, true, sizeof(finally_run_t));

	zval_ptr_dtor(&handlers);

	run->target = target;

	if (target != NULL) {
		GC_ADDREF(target);
	}

	run->iterator.extended_dtor = finally_run_dtor;

	/* A refused worker took the run's empty scope with it. */
	if (UNEXPECTED(!async_iterator_run_in_coroutine(&run->iterator))) {
		ZEND_ASYNC_MICROTASK_RELEASE(&run->iterator.microtask);
		return false;
	}

	return true;
}

///////////////////////////////////////////////////////////////////
/// Methods
///////////////////////////////////////////////////////////////////

#define THIS_COROUTINE (async_coroutine_from_object(Z_OBJ_P(ZEND_THIS)))
#define THIS_FLAGS (THIS_COROUTINE->coroutine.flags)

ZEND_METHOD(Async_Coroutine, getId)
{
	ZEND_PARSE_PARAMETERS_NONE();

	RETURN_LONG(Z_OBJ_P(ZEND_THIS)->handle);
}

ZEND_METHOD(Async_Coroutine, asHiPriority)
{
	ZEND_PARSE_PARAMETERS_NONE();

	/* Never moves a queued coroutine: the next enqueue puts it at the front, once (D20, D35). */
	THIS_FLAGS |= ASYNC_COROUTINE_F_HI_PRIORITY;

	RETURN_OBJ_COPY(Z_OBJ_P(ZEND_THIS));
}

ZEND_METHOD(Async_Coroutine, getResult)
{
	ZEND_PARSE_PARAMETERS_NONE();

	async_coroutine_t *coroutine = THIS_COROUTINE;

	if (!ZEND_COROUTINE_IS_FINISHED(&coroutine->coroutine) || Z_ISUNDEF(coroutine->coroutine.result)) {
		RETURN_NULL();
	}

	RETURN_COPY_DEREF(&coroutine->coroutine.result);
}

ZEND_METHOD(Async_Coroutine, getException)
{
	ZEND_PARSE_PARAMETERS_NONE();

	async_coroutine_t *coroutine = THIS_COROUTINE;

	if (!ZEND_COROUTINE_IS_FINISHED(&coroutine->coroutine) || coroutine->coroutine.exception == NULL) {
		RETURN_NULL();
	}

	/* Read, the exception is observed: neither its release nor the request's end reports it. */
	coroutine->coroutine.flags |= ASYNC_COROUTINE_F_EXC_CAUGHT;
	RETURN_OBJ_COPY(coroutine->coroutine.exception);
}

ZEND_METHOD(Async_Coroutine, getSpawnFileAndLine)
{
	ZEND_PARSE_PARAMETERS_NONE();

	const async_coroutine_t *coroutine = THIS_COROUTINE;

	array_init_size(return_value, 2);

	if (coroutine->coroutine.filename != NULL) {
		add_next_index_str(return_value, zend_string_copy(coroutine->coroutine.filename));
	} else {
		add_next_index_null(return_value);
	}

	add_next_index_long(return_value, coroutine->coroutine.lineno);
}

ZEND_METHOD(Async_Coroutine, getSpawnLocation)
{
	ZEND_PARSE_PARAMETERS_NONE();

	const async_coroutine_t *coroutine = THIS_COROUTINE;

	if (coroutine->coroutine.filename == NULL) {
		RETURN_STRING("unknown");
	}

	RETURN_STR(zend_strpprintf(0, "%s:%" PRIu32, ZSTR_VAL(coroutine->coroutine.filename), coroutine->coroutine.lineno));
}

/* The state methods are formulas over the flags word (S3.md section 2). */

ZEND_METHOD(Async_Coroutine, isStarted)
{
	ZEND_PARSE_PARAMETERS_NONE();

	RETURN_BOOL(THIS_FLAGS & ZEND_COROUTINE_F_STARTED);
}

ZEND_METHOD(Async_Coroutine, isQueued)
{
	ZEND_PARSE_PARAMETERS_NONE();

	const uint32_t flags = THIS_FLAGS;
	/* A cancellation before the first run skips the body: such a coroutine waits only to finish. */
	const bool cancelled_before_run = (flags & ZEND_COROUTINE_F_CANCELLED) && !(flags & ZEND_COROUTINE_F_STARTED);

	RETURN_BOOL((flags & ZEND_COROUTINE_STATUS_MASK) == ZEND_COROUTINE_STATUS_QUEUED && !cancelled_before_run);
}

ZEND_METHOD(Async_Coroutine, isRunning)
{
	ZEND_PARSE_PARAMETERS_NONE();

	const uint32_t flags = THIS_FLAGS;

	RETURN_BOOL((flags & ZEND_COROUTINE_F_STARTED) &&
				(flags & ZEND_COROUTINE_STATUS_MASK) != ZEND_COROUTINE_STATUS_FINISHED);
}

/* A yield is QUEUED and suspended at once (D6); the running coroutine is not suspended (D18). */
ZEND_METHOD(Async_Coroutine, isSuspended)
{
	ZEND_PARSE_PARAMETERS_NONE();

	const uint32_t status = THIS_FLAGS & ZEND_COROUTINE_STATUS_MASK;

	RETURN_BOOL(status == ZEND_COROUTINE_STATUS_QUEUED || status == ZEND_COROUTINE_STATUS_SUSPENDED);
}

ZEND_METHOD(Async_Coroutine, isCancelled)
{
	ZEND_PARSE_PARAMETERS_NONE();

	const uint32_t flags = THIS_FLAGS;

	RETURN_BOOL((flags & ZEND_COROUTINE_F_CANCELLED) &&
				(flags & ZEND_COROUTINE_STATUS_MASK) == ZEND_COROUTINE_STATUS_FINISHED);
}

ZEND_METHOD(Async_Coroutine, isCancellationRequested)
{
	ZEND_PARSE_PARAMETERS_NONE();

	const async_coroutine_t *coroutine = THIS_COROUTINE;
	const uint32_t flags = coroutine->coroutine.flags;

	RETURN_BOOL(((flags & ZEND_COROUTINE_F_CANCELLED) &&
				 (flags & ZEND_COROUTINE_STATUS_MASK) != ZEND_COROUTINE_STATUS_FINISHED) ||
				coroutine->deferred_cancellation != NULL);
}

ZEND_METHOD(Async_Coroutine, isCompleted)
{
	ZEND_PARSE_PARAMETERS_NONE();

	RETURN_BOOL((THIS_FLAGS & ZEND_COROUTINE_STATUS_MASK) == ZEND_COROUTINE_STATUS_FINISHED);
}

zend_execute_data *async_coroutine_suspend_frame(async_coroutine_t *coroutine)
{
	zend_execute_data *execute_data = ZEND_ASYNC_COROUTINE_EXECUTE_DATA(&coroutine->coroutine);

	while (execute_data != NULL && (execute_data->func == NULL || !ZEND_USER_CODE(execute_data->func->type))) {
		execute_data = execute_data->prev_execute_data;
	}

	return execute_data;
}

ZEND_METHOD(Async_Coroutine, getSuspendFileAndLine)
{
	ZEND_PARSE_PARAMETERS_NONE();

	const zend_execute_data *suspend_frame = async_coroutine_suspend_frame(THIS_COROUTINE);

	array_init_size(return_value, 2);

	if (suspend_frame == NULL) {
		add_next_index_null(return_value);
		add_next_index_long(return_value, 0);
		return;
	}

	add_next_index_str(return_value, zend_string_copy(suspend_frame->func->op_array.filename));
	add_next_index_long(return_value, suspend_frame->opline->lineno);
}

ZEND_METHOD(Async_Coroutine, getSuspendLocation)
{
	ZEND_PARSE_PARAMETERS_NONE();

	const zend_execute_data *suspend_frame = async_coroutine_suspend_frame(THIS_COROUTINE);

	if (suspend_frame == NULL) {
		RETURN_STRING("unknown");
	}

	RETURN_STR(zend_strpprintf(
			0, "%s:%" PRIu32, ZSTR_VAL(suspend_frame->func->op_array.filename), suspend_frame->opline->lineno));
}

ZEND_METHOD(Async_Coroutine, getAwaitingInfo)
{
	ZEND_PARSE_PARAMETERS_NONE();

	zend_array *info = ZEND_ASYNC_GET_AWAITING_INFO(&THIS_COROUTINE->coroutine);

	if (info == NULL) {
		RETURN_EMPTY_ARRAY();
	}

	RETURN_ARR(info);
}

/* The backtrace of the parked stack: the engine walks it from the parked frame as if it ran. */
ZEND_METHOD(Async_Coroutine, getTrace)
{
	zend_long options = DEBUG_BACKTRACE_PROVIDE_OBJECT, limit = 0;

	ZEND_PARSE_PARAMETERS_START(0, 2)
		Z_PARAM_OPTIONAL
		Z_PARAM_LONG(options)
		Z_PARAM_LONG(limit)
	ZEND_PARSE_PARAMETERS_END();

	zend_execute_data *parked_frame = ZEND_ASYNC_COROUTINE_EXECUTE_DATA(&THIS_COROUTINE->coroutine);

	if (parked_frame == NULL) {
		RETURN_NULL();
	}

	zend_execute_data *running_frame = EG(current_execute_data);

	EG(current_execute_data) = parked_frame;
	zend_fetch_debug_backtrace(return_value, 0, (int) options, (int) limit);
	EG(current_execute_data) = running_frame;
}

ZEND_METHOD(Async_Coroutine, cancel)
{
	zend_object *cancellation = NULL;

	ZEND_PARSE_PARAMETERS_START(0, 1)
		Z_PARAM_OPTIONAL
		Z_PARAM_OBJ_OF_CLASS_OR_NULL(cancellation, async_ce_cancellation)
	ZEND_PARSE_PARAMETERS_END();

	async_coroutine_cancel(THIS_COROUTINE, cancellation, false, false);
}

/* TrueAsync's METHOD(finally), coroutine.c:1381-1420: a finished coroutine calls the closure at once, in
 * the caller, which gets what it throws. */
ZEND_METHOD(Async_Coroutine, finally)
{
	zval *callback;

	ZEND_PARSE_PARAMETERS_START(1, 1)
		Z_PARAM_OBJECT_OF_CLASS(callback, zend_ce_closure)
	ZEND_PARSE_PARAMETERS_END();

	async_coroutine_t *coroutine = THIS_COROUTINE;

	if (UNEXPECTED(ZEND_COROUTINE_IS_FINISHED(&coroutine->coroutine))) {
		zval argument;
		zval retval;

		ZVAL_OBJ(&argument, &coroutine->std);
		call_user_function(NULL, NULL, callback, &retval, 1, &argument);
		zval_ptr_dtor(&retval);
		return;
	}

	if (coroutine->finally_handlers == NULL) {
		coroutine->finally_handlers = zend_new_array(1);
	}

	Z_ADDREF_P(callback);
	zend_hash_next_index_insert_new(coroutine->finally_handlers, callback);
}

void async_register_coroutine_ce(zend_class_entry *completable_interface)
{
	async_ce_coroutine = register_class_Async_Coroutine(completable_interface);
	async_ce_coroutine->create_object = coroutine_object_create;
	async_ce_coroutine->default_object_handlers = &coroutine_handlers;

	memcpy(&coroutine_handlers, zend_get_std_object_handlers(), sizeof(zend_object_handlers));
	coroutine_handlers.offset = offsetof(async_coroutine_t, std);
	coroutine_handlers.dtor_obj = coroutine_object_destroy;
	coroutine_handlers.free_obj = coroutine_object_free;
	coroutine_handlers.get_gc = coroutine_object_gc;
	coroutine_handlers.clone_obj = NULL;
	coroutine_handlers.get_constructor = coroutine_object_get_constructor;
}
