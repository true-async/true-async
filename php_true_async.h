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

/* Notifies nested deeper than this are refused; a coroutine's finish nests at most one level
 * (a callback that resolves another awaitable) in S3. */
#define ASYNC_NOTIFY_DEPTH_MAX 32

/* A notify in progress: async_callbacks_remove() corrects its cursor. */
typedef struct
{
	async_callbacks_vector_t *vector;
	uint32_t cursor; /* index of the next callback to run */
} async_notify_frame_t;

ZEND_BEGIN_MODULE_GLOBALS(true_async)
	async_notify_frame_t notify_stack[ASYNC_NOTIFY_DEPTH_MAX];
	uint32_t notify_depth;
	uint32_t handler_id_seq; /* last finish handler id; 0 is never handed out */
	bool bailing_out;        /* the scheduler unwinds every coroutine after a bailout */
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
