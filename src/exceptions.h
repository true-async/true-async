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
extern zend_class_entry *async_ce_async_exception;
extern zend_class_entry *async_ce_deadlock_error;
extern zend_class_entry *async_ce_composite_exception;

void async_register_exceptions_ce(void);

/* Appends `exception` to the composite's list; `transfer` hands over the caller's reference,
 * otherwise one is added. */
void async_composite_exception_add_exception(zend_object *composite, zend_object *exception, bool transfer);

#endif /* TRUE_ASYNC_EXCEPTIONS_H */
