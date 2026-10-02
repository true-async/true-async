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
#ifndef PHP_TRUE_ASYNC_H
#define PHP_TRUE_ASYNC_H

/* Kept at the repository root: a static build of php-src finds the module entry by grepping the
 * *.h files of ext/<name> for "phpext_", without descending into subdirectories. */

extern zend_module_entry true_async_module_entry;
#define phpext_true_async_ptr &true_async_module_entry

#define PHP_TRUE_ASYNC_VERSION "0.1.0-dev"

#include "src/true_async_API.h"
#include "Zend/zend_smart_str_public.h"

ZEND_BEGIN_MODULE_GLOBALS(true_async)
	uint32_t handler_id_seq; /* last finish handler id; 0 is never handed out */
	bool debug_deadlock;     /* true_async.debug_deadlock: the deadlock report lists every coroutine */
#ifdef TRUE_ASYNC_TEST_HOOKS
	smart_str *test_trace; /* where a test hook's C callback writes; NULL outside a scenario */
#endif
ZEND_END_MODULE_GLOBALS(true_async)

ZEND_EXTERN_MODULE_GLOBALS(true_async)
#define ASYNC_G(v) ZEND_MODULE_GLOBALS_ACCESSOR(true_async, v)

#if defined(ZTS) && defined(COMPILE_DL_TRUE_ASYNC)
ZEND_TSRMLS_CACHE_EXTERN()
#endif

#endif /* PHP_TRUE_ASYNC_H */
