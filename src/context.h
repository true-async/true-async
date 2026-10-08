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
#ifndef TRUE_ASYNC_CONTEXT_H
#define TRUE_ASYNC_CONTEXT_H

#include "php.h"
#include "Zend/zend_async_API.h"

/* Async\Context (dev/plans/S9-context.md, section 2): the PHP class over the core's
 * zend_async_context_t, which owns the tables and their operations. */

extern zend_class_entry *async_ce_context;
extern zend_class_entry *async_ce_context_exception;

void async_register_context_ce(void);

/* The factory of the core's zend_async_new_context_fn: a new empty Context, with one reference for
 * the caller. */
zend_object *async_context_new(void);

#endif /* TRUE_ASYNC_CONTEXT_H */
