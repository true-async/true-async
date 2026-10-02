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
#include "coroutine.h"
#include "exceptions.h"
#include "coroutine_arginfo.h"

zend_class_entry *async_ce_coroutine = NULL;

static zend_object_handlers coroutine_handlers;

static zend_object *coroutine_object_create(zend_class_entry *class_entry)
{
	/* 280 B: a class without properties takes the inline properties slot off the size. */
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

	zend_get_gc_buffer_add_ht(buffer, &coroutine->coroutine.internal_context);

	/* The parked stack is not walked: trial deletion keeps whatever its frames hold, since it
	 * cannot explain those references. */
	zend_get_gc_buffer_use(buffer, table, num);

	return NULL;
}

/* The methods come with their steps of dev/PLAN.md: the state methods with S3.5, the suspend
 * location and trace with S3.6, getAwaitingInfo with S3.7, cancel with S3.8. */
#define COROUTINE_METHOD_PENDING(name) \
	ZEND_METHOD(Async_Coroutine, name) \
	{ \
		ZEND_PARSE_PARAMETERS_NONE(); \
		zend_throw_error(NULL, "Async\\Coroutine::%s() is not implemented yet", #name); \
	}

COROUTINE_METHOD_PENDING(getId)
COROUTINE_METHOD_PENDING(asHiPriority)
COROUTINE_METHOD_PENDING(getResult)
COROUTINE_METHOD_PENDING(getException)
COROUTINE_METHOD_PENDING(getSpawnFileAndLine)
COROUTINE_METHOD_PENDING(getSpawnLocation)
COROUTINE_METHOD_PENDING(getSuspendFileAndLine)
COROUTINE_METHOD_PENDING(getSuspendLocation)
COROUTINE_METHOD_PENDING(isStarted)
COROUTINE_METHOD_PENDING(isQueued)
COROUTINE_METHOD_PENDING(isRunning)
COROUTINE_METHOD_PENDING(isSuspended)
COROUTINE_METHOD_PENDING(isCancelled)
COROUTINE_METHOD_PENDING(isCancellationRequested)
COROUTINE_METHOD_PENDING(isCompleted)
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
}
