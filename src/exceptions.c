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
#include "zend_exceptions.h"
#include "exceptions.h"
#include "exceptions_arginfo.h"

zend_class_entry *async_ce_cancellation = NULL;
zend_class_entry *async_ce_operation_canceled = NULL;
zend_class_entry *async_ce_timeout_exception = NULL;
zend_class_entry *async_ce_async_exception = NULL;
zend_class_entry *async_ce_deadlock_error = NULL;
static zend_class_entry *async_ce_composite_exception = NULL;

/* The typed `$exceptions` property as an array, or NULL before the first write. Read with silent=1:
 * an uninitialised one gives UNDEF instead of an error, so an empty composite reads back as [];
 * unserialize() can leave a reference in the slot. */
static zval *composite_exceptions(zend_object *composite)
{
	zval *exceptions = zend_read_property(async_ce_composite_exception, composite, ZEND_STRL("exceptions"), true, NULL);

	ZVAL_DEREF(exceptions);

	return Z_TYPE_P(exceptions) == IS_ARRAY ? exceptions : NULL;
}

static void composite_exception_add_exception(zend_object *composite, zend_object *exception)
{
	zval *exceptions = composite_exceptions(composite);
	zval element;

	ZVAL_OBJ_COPY(&element, exception);

	if (exceptions != NULL) {
		SEPARATE_ARRAY(exceptions);

		/* The array may come from user code (reflection, unserialize()) with PHP_INT_MAX taken. */
		if (UNEXPECTED(zend_hash_next_index_insert(Z_ARRVAL_P(exceptions), &element) == NULL)) {
			zval_ptr_dtor(&element);
			zend_cannot_add_element();
		}

		return;
	}

	/* The first write goes through the property API, which initialises the typed property. */
	zval exceptions_array;
	array_init(&exceptions_array);
	zend_hash_next_index_insert_new(Z_ARRVAL(exceptions_array), &element);
	zend_update_property(async_ce_composite_exception, composite, ZEND_STRL("exceptions"), &exceptions_array);
	zval_ptr_dtor(&exceptions_array);
}

zend_object *async_new_exception(zend_class_entry *exception_ce, const char *format, ...)
{
	zval exception;
	zval message;
	va_list args;

	object_init_ex(&exception, exception_ce);

	va_start(args, format);
	ZVAL_STR(&message, zend_vstrpprintf(0, format, args));
	va_end(args);

	zend_update_property_ex(exception_ce, Z_OBJ(exception), ZSTR_KNOWN(ZEND_STR_MESSAGE), &message);
	zval_ptr_dtor(&message);

	return Z_OBJ(exception);
}

ZEND_METHOD(Async_CompositeException, addException)
{
	zend_object *exception;

	ZEND_PARSE_PARAMETERS_START(1, 1)
		Z_PARAM_OBJ_OF_CLASS(exception, zend_ce_throwable)
	ZEND_PARSE_PARAMETERS_END();

	composite_exception_add_exception(Z_OBJ_P(ZEND_THIS), exception);
}

ZEND_METHOD(Async_CompositeException, getExceptions)
{
	ZEND_PARSE_PARAMETERS_NONE();

	zval *exceptions = composite_exceptions(Z_OBJ_P(ZEND_THIS));

	if (exceptions != NULL) {
		RETURN_COPY(exceptions);
	}

	RETURN_EMPTY_ARRAY();
}

void async_register_exceptions_ce(void)
{
	async_ce_cancellation = register_class_Async_AsyncCancellation(zend_ce_error);
	async_ce_operation_canceled = register_class_Async_OperationCanceledException(async_ce_cancellation);
	async_ce_timeout_exception = register_class_Async_TimeoutException(zend_ce_exception);
	async_ce_async_exception = register_class_Async_AsyncException(zend_ce_exception);
	async_ce_deadlock_error = register_class_Async_DeadlockError(zend_ce_error);
	async_ce_composite_exception = register_class_Async_CompositeException(zend_ce_exception);
}
