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
#ifndef TRUE_ASYNC_EXCEPTIONS_H
#define TRUE_ASYNC_EXCEPTIONS_H

#include "php.h"

extern zend_class_entry *async_ce_cancellation;
extern zend_class_entry *async_ce_operation_canceled;
extern zend_class_entry *async_ce_timeout_exception;
extern zend_class_entry *async_ce_async_exception;
extern zend_class_entry *async_ce_deadlock_error;

void async_register_exceptions_ce(void);

/* A new exception of `exception_ce` with a printf-formatted message, not thrown; the caller owns
 * the reference (TrueAsync's async_new_exception, exceptions.c:83). */
zend_object *async_new_exception(zend_class_entry *exception_ce, const char *format, ...)
		ZEND_ATTRIBUTE_FORMAT(printf, 2, 3);

#endif /* TRUE_ASYNC_EXCEPTIONS_H */
