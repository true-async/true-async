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
zend_class_entry *async_ce_async_exception = NULL;
zend_class_entry *async_ce_deadlock_error = NULL;
zend_class_entry *async_ce_composite_exception = NULL;

/* The typed `$exceptions` property, read with silent=1: an uninitialised one gives UNDEF instead
 * of an error, so an empty composite reads back as []. */
static zval *composite_exceptions(zend_object *composite)
{
	return zend_read_property(async_ce_composite_exception, composite, ZEND_STRL("exceptions"), true, NULL);
}

void async_composite_exception_add_exception(zend_object *composite, zend_object *exception, const bool transfer)
{
	zval *exceptions = composite_exceptions(composite);
	zval element;

	if (!transfer) {
		GC_ADDREF(exception);
	}

	ZVAL_OBJ(&element, exception);

	if (exceptions != NULL && Z_TYPE_P(exceptions) == IS_ARRAY) {
		SEPARATE_ARRAY(exceptions);
		zend_hash_next_index_insert_new(Z_ARRVAL_P(exceptions), &element);
		return;
	}

	/* The first write goes through the property API, which initialises the typed property. */
	zval list;
	array_init(&list);
	zend_hash_next_index_insert_new(Z_ARRVAL(list), &element);
	zend_update_property(async_ce_composite_exception, composite, ZEND_STRL("exceptions"), &list);
	zval_ptr_dtor(&list);
}

ZEND_METHOD(Async_CompositeException, addException)
{
	zend_object *exception;

	ZEND_PARSE_PARAMETERS_START(1, 1)
		Z_PARAM_OBJ_OF_CLASS(exception, zend_ce_throwable)
	ZEND_PARSE_PARAMETERS_END();

	async_composite_exception_add_exception(Z_OBJ_P(ZEND_THIS), exception, false);
}

ZEND_METHOD(Async_CompositeException, getExceptions)
{
	ZEND_PARSE_PARAMETERS_NONE();

	zval *exceptions = composite_exceptions(Z_OBJ_P(ZEND_THIS));

	if (exceptions != NULL && Z_TYPE_P(exceptions) == IS_ARRAY) {
		RETURN_COPY(exceptions);
	}

	RETURN_EMPTY_ARRAY();
}

void async_register_exceptions_ce(void)
{
	async_ce_cancellation = register_class_Async_AsyncCancellation(zend_ce_error);
	async_ce_async_exception = register_class_Async_AsyncException(zend_ce_exception);
	async_ce_deadlock_error = register_class_Async_DeadlockError(zend_ce_error);
	async_ce_composite_exception = register_class_Async_CompositeException(zend_ce_exception);
}
