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
#ifndef TRUE_ASYNC_COROUTINE_H
#define TRUE_ASYNC_COROUTINE_H

#include "php.h"
#include "true_async_API.h"

/* Defined by the steps that fill them: the fiber context in S3.5, the switch handlers in S3.10,
 * the scope in S9. */
typedef struct _async_fiber_context_s async_fiber_context_t;
typedef struct _async_scope_s async_scope_t;
typedef struct _async_awaiting_info_vector_s async_awaiting_info_vector_t;
typedef struct _async_coroutine_switch_handlers_vector_s async_coroutine_switch_handlers_vector_t;

/* The coroutine and its PHP object in one allocation (dev/plans/S3.md, section 3.1). */
struct _async_coroutine_s
{
	zend_coroutine_t coroutine;           /* flags at offset 0: the awaitable type bit is 0 */
	async_fiber_context_t *fiber_context; /* execute_data stored at suspend entry */
	async_callbacks_vector_t callbacks;   /* waiters' records and finish handlers */
	async_waker_t waker;
	async_scope_t *scope;                                      /* NULL until S9 */
	zend_object *deferred_cancellation;                        /* the cancel that arrived inside protect() */
	async_awaiting_info_vector_t *awaiting_info;               /* lazy; foreign RFC registrations */
	async_coroutine_switch_handlers_vector_t *switch_handlers; /* lazy */
	zend_object std;                                           /* last: the properties table runs past the end */
};

/* The sizes of section 3.1, checked at compile time on 64-bit targets: 304 B, allocated as 288 in
 * the 320 B bin. */
#if SIZEOF_SIZE_T == 8
typedef char async_coroutine_size_check[sizeof(async_coroutine_t) == 304 ? 1 : -1];
typedef char async_coroutine_std_offset_check[offsetof(async_coroutine_t, std) == 248 ? 1 : -1];
#endif

extern zend_class_entry *async_ce_coroutine;

static zend_always_inline async_coroutine_t *async_coroutine_from_object(zend_object *object)
{
	return (async_coroutine_t *) ((char *) object - offsetof(async_coroutine_t, std));
}

void async_register_coroutine_ce(zend_class_entry *completable);

#endif /* TRUE_ASYNC_COROUTINE_H */
