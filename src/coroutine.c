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
#include "coroutine.h"
#include "exceptions.h"
#include "coroutine_arginfo.h"

zend_class_entry *async_ce_coroutine = NULL;

static zend_object_handlers coroutine_handlers;

static zend_object *coroutine_object_create(zend_class_entry *class_entry)
{
	/* 288 B: a class without properties takes the inline properties slot off the size. */
	async_coroutine_t *coroutine = zend_object_alloc(sizeof(async_coroutine_t), class_entry);

	ZVAL_UNDEF(&coroutine->coroutine.result);
	ZVAL_UNDEF(&coroutine->waker.result);
	coroutine->coroutine.object_offset = offsetof(async_coroutine_t, std);
	zend_async_internal_context_init(&coroutine->coroutine);

	zend_object_std_init(&coroutine->std, class_entry);
	object_properties_init(&coroutine->std, class_entry);

	return &coroutine->std;
}

static void coroutine_object_free(zend_object *object)
{
	async_coroutine_t *coroutine = async_coroutine_from_object(object);

	/* The steps that fill these fields release them before the object dies. */
	ZEND_ASSERT(coroutine->fiber_context == NULL);
	ZEND_ASSERT(coroutine->scope == NULL);
	ZEND_ASSERT(coroutine->awaiting_info == NULL);
	ZEND_ASSERT(coroutine->switch_handlers == NULL);

	/* Whoever attached itself to this coroutine (a fiber, say) lets go of it first. */
	if (coroutine->coroutine.extended_dispose != NULL) {
		coroutine->coroutine.extended_dispose(&coroutine->coroutine);
	}

	async_callbacks_free((async_awaitable_t *) coroutine, &coroutine->callbacks);

	if (coroutine->coroutine.fcall != NULL) {
		ZEND_ASYNC_FCALL_FREE(coroutine->coroutine.fcall);
		coroutine->coroutine.fcall = NULL;
	}

	if (coroutine->coroutine.filename != NULL) {
		zend_string_release_ex(coroutine->coroutine.filename, false);
	}

	if (coroutine->coroutine.exception != NULL) {
		OBJ_RELEASE(coroutine->coroutine.exception);
	}

	if (coroutine->deferred_cancellation != NULL) {
		OBJ_RELEASE(coroutine->deferred_cancellation);
	}

	if (coroutine->waker.error != NULL) {
		OBJ_RELEASE(coroutine->waker.error);
	}

	zval_ptr_dtor(&coroutine->waker.result);
	zval_ptr_dtor(&coroutine->coroutine.result);
	zend_async_internal_context_destroy(&coroutine->coroutine);
	zend_async_context_destroy(&coroutine->coroutine);
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
	zend_get_gc_buffer *buffer = zend_get_gc_buffer_create();

	zend_get_gc_buffer_add_zval(buffer, &coroutine->coroutine.result);
	zend_get_gc_buffer_add_zval(buffer, &coroutine->waker.result);

	/* An exception's trace can point back at this coroutine; without these edges such a cycle
	 * never collects. */
	if (coroutine->coroutine.exception != NULL) {
		zend_get_gc_buffer_add_obj(buffer, coroutine->coroutine.exception);
	}

	if (coroutine->deferred_cancellation != NULL) {
		zend_get_gc_buffer_add_obj(buffer, coroutine->deferred_cancellation);
	}

	if (coroutine->waker.error != NULL) {
		zend_get_gc_buffer_add_obj(buffer, coroutine->waker.error);
	}

	const zend_fcall_t *fcall = coroutine->coroutine.fcall;

	if (fcall != NULL) {
		zend_get_gc_buffer_add_zval(buffer, (zval *) &fcall->fci.function_name);

		for (uint32_t i = 0; i < fcall->fci.param_count; i++) {
			zend_get_gc_buffer_add_zval(buffer, &fcall->fci.params[i]);
		}

		if (fcall->fci.named_params != NULL) {
			zend_get_gc_buffer_add_ht(buffer, fcall->fci.named_params);
		}
	}

	if (coroutine->coroutine.context != NULL) {
		zend_get_gc_buffer_add_obj(buffer, coroutine->coroutine.context);
	}

	/* The table is a field of this block, not a refcounted array: the collector gets its values, never
	 * the table itself, or it would free the table out of the middle of the coroutine. */
	zval *value;

	ZEND_HASH_FOREACH_VAL(&coroutine->coroutine.internal_context, value)
	{
		zend_get_gc_buffer_add_zval(buffer, value);
	}
	ZEND_HASH_FOREACH_END();

	/* The parked stack is not walked: trial deletion keeps whatever its frames hold, since it
	 * cannot explain those references. */
	zend_get_gc_buffer_use(buffer, table, num);

	return NULL;
}

///////////////////////////////////////////////////////////////////
/// Running and finishing
///////////////////////////////////////////////////////////////////

/* The request's exit exception (S3.md section 6): a later one takes the earlier as its previous.
 * Takes a reference. */
static void exit_exception_add(zend_object *exception)
{
	if (UNEXPECTED(ZEND_ASYNC_EXIT_EXCEPTION != NULL)) {
		zend_exception_set_previous(exception, ZEND_ASYNC_EXIT_EXCEPTION);
	}

	ZEND_ASYNC_EXIT_EXCEPTION = exception;
}

/* The exception pending in EG becomes the coroutine's outcome. Over an outcome already stored (a
 * cancellation delivered while the body ran on, S3.8) it keeps that one as its previous, unless it
 * is itself a cancellation. An exit unwinds the coroutine and is no outcome; what exit() in a
 * coroutine does to the request is S3.8's. */
static void coroutine_take_exception(async_coroutine_t *coroutine)
{
	zend_object *exception = EG(exception);

	GC_ADDREF(exception);
	zend_clear_exception();

	if (UNEXPECTED(zend_is_graceful_exit(exception) || zend_is_unwind_exit(exception))) {
		OBJ_RELEASE(exception);
		return;
	}

	zend_object *outcome = coroutine->coroutine.exception;

	if (UNEXPECTED(outcome != NULL && instanceof_function(exception->ce, async_ce_cancellation))) {
		OBJ_RELEASE(exception);
		return;
	}

	if (UNEXPECTED(outcome != NULL)) {
		zend_exception_set_previous(exception, outcome);
	}

	coroutine->coroutine.exception = exception;
}

void async_coroutine_execute(async_coroutine_t *coroutine)
{
	zend_coroutine_t *zend_coroutine = &coroutine->coroutine;

	ZEND_ASSERT(zend_coroutine == ZEND_ASYNC_CURRENT_COROUTINE && ZEND_COROUTINE_IS_RUNNING(zend_coroutine));
	ZEND_ASSERT((zend_coroutine->fcall != NULL) != (zend_coroutine->internal_entry != NULL));

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

	if (UNEXPECTED(is_bailout)) {
		/* Finished and maybe freed: not current for the bailout's drop (TrueAsync, coroutine.c:567). */
		ZEND_ASYNC_CURRENT_COROUTINE = NULL;
		zend_bailout();
	}
}

void async_coroutine_finalize(async_coroutine_t *coroutine)
{
	zend_coroutine_t *zend_coroutine = &coroutine->coroutine;
	const bool is_bailout = (zend_coroutine->flags & ASYNC_COROUTINE_F_BAILOUT) != 0;

	ZEND_ASSERT(coroutine->waker.wait == NULL && "a coroutine finishes with no wait linked");

	if (UNEXPECTED(EG(exception) != NULL)) {
		coroutine_take_exception(coroutine);
	}

	ZEND_COROUTINE_SET_STATUS(zend_coroutine, ZEND_COROUTINE_STATUS_FINISHED);

	/* The context stays with the loop that ran the body; main's copy is its caller's to free. */
	coroutine->fiber_context = NULL;

	/* The notify may drop every other reference to the object, and a finish handler may clear the
	 * exception (R:84): both live until the end of this function. */
	zend_object *exception = zend_coroutine->exception;

	GC_ADDREF(&coroutine->std);

	if (exception != NULL) {
		GC_ADDREF(exception);
	}

	zend_coroutine->flags &= ~ASYNC_COROUTINE_F_EXCEPTION_HANDLED;
	async_callbacks_notify((async_awaitable_t *) coroutine, &coroutine->callbacks, &zend_coroutine->result, exception);

	/* Observed: a waiter took the exception, or a finish handler cleared it. */
	if (exception != NULL &&
		((zend_coroutine->flags & ASYNC_COROUTINE_F_EXCEPTION_HANDLED) || zend_coroutine->exception == NULL)) {
		zend_coroutine->flags |= ASYNC_COROUTINE_F_EXC_CAUGHT;
	}

	async_callbacks_free((async_awaitable_t *) coroutine, &coroutine->callbacks);

	if (zend_coroutine->extended_dispose != NULL) {
		const zend_coroutine_dispose_fn dispose = zend_coroutine->extended_dispose;
		zend_coroutine->extended_dispose = NULL;
		dispose(zend_coroutine);
	}

	zend_hash_index_del(&ASYNC_G(coroutines), coroutine->std.handle);

	/* Nobody can observe the exception when only the scheduler's reference and this function's are
	 * left, or for main and a fiber, which nobody awaits through the object: it ends the request. A
	 * cancellation is the scheduler's own doing; after a bailout the request ends anyway. */
	if (UNEXPECTED(exception != NULL && !is_bailout && !(zend_coroutine->flags & ASYNC_COROUTINE_F_EXC_CAUGHT) &&
				   !instanceof_function(exception->ce, async_ce_cancellation) &&
				   (GC_REFCOUNT(&coroutine->std) <= 2 || ZEND_COROUTINE_IS_MAIN(zend_coroutine) ||
					ZEND_COROUTINE_IS_FIBER(zend_coroutine)))) {
		zend_coroutine->flags |= ASYNC_COROUTINE_F_EXC_CAUGHT;
		GC_ADDREF(exception);
		exit_exception_add(exception);
	}

	/* What the waiters and finish handlers threw ends the request too. */
	if (UNEXPECTED(EG(exception) != NULL)) {
		zend_object *thrown = EG(exception);
		GC_ADDREF(thrown);
		zend_clear_exception();
		exit_exception_add(thrown);
	}

	if (exception != NULL) {
		OBJ_RELEASE(exception);
	}

	OBJ_RELEASE(&coroutine->std);
	/* The scheduler's birth reference. */
	OBJ_RELEASE(&coroutine->std);
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

	const async_coroutine_t *coroutine = THIS_COROUTINE;

	if (!ZEND_COROUTINE_IS_FINISHED(&coroutine->coroutine) || Z_ISUNDEF(coroutine->coroutine.result)) {
		RETURN_NULL();
	}

	RETURN_COPY(&coroutine->coroutine.result);
}

ZEND_METHOD(Async_Coroutine, getException)
{
	ZEND_PARSE_PARAMETERS_NONE();

	const async_coroutine_t *coroutine = THIS_COROUTINE;

	if (!ZEND_COROUTINE_IS_FINISHED(&coroutine->coroutine) || coroutine->coroutine.exception == NULL) {
		RETURN_NULL();
	}

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

/* The methods of later steps of dev/PLAN.md: the suspend location with S3.6, getAwaitingInfo with
 * S3.7. */
#define COROUTINE_METHOD_PENDING(name) \
	ZEND_METHOD(Async_Coroutine, name) \
	{ \
		ZEND_PARSE_PARAMETERS_NONE(); \
		zend_throw_error(NULL, "Async\\Coroutine::%s() is not implemented yet", #name); \
	}

COROUTINE_METHOD_PENDING(getSuspendFileAndLine)
COROUTINE_METHOD_PENDING(getSuspendLocation)
COROUTINE_METHOD_PENDING(getAwaitingInfo)

ZEND_METHOD(Async_Coroutine, getTrace)
{
	zend_long options = DEBUG_BACKTRACE_PROVIDE_OBJECT, limit = 0;

	ZEND_PARSE_PARAMETERS_START(0, 2)
		Z_PARAM_OPTIONAL
		Z_PARAM_LONG(options)
		Z_PARAM_LONG(limit)
	ZEND_PARSE_PARAMETERS_END();

	zend_throw_error(NULL, "Async\\Coroutine::getTrace() is not implemented yet");
}

ZEND_METHOD(Async_Coroutine, cancel)
{
	zend_object *cancellation = NULL;

	ZEND_PARSE_PARAMETERS_START(0, 1)
		Z_PARAM_OPTIONAL
		Z_PARAM_OBJ_OF_CLASS_OR_NULL(cancellation, async_ce_cancellation)
	ZEND_PARSE_PARAMETERS_END();

	zend_throw_error(NULL, "Async\\Coroutine::cancel() is not implemented yet");
}

void async_register_coroutine_ce(zend_class_entry *completable)
{
	async_ce_coroutine = register_class_Async_Coroutine(completable);
	async_ce_coroutine->create_object = coroutine_object_create;
	async_ce_coroutine->default_object_handlers = &coroutine_handlers;

	memcpy(&coroutine_handlers, zend_get_std_object_handlers(), sizeof(zend_object_handlers));
	coroutine_handlers.offset = offsetof(async_coroutine_t, std);
	coroutine_handlers.free_obj = coroutine_object_free;
	coroutine_handlers.get_gc = coroutine_object_gc;
	coroutine_handlers.clone_obj = NULL;
	coroutine_handlers.get_constructor = coroutine_object_get_constructor;
}
