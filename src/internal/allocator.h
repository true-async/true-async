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
#ifndef TRUE_ASYNC_ALLOCATOR_H
#define TRUE_ASYNC_ALLOCATOR_H

#include <stddef.h>

/* Memory functions a container allocates through: request memory (true_async_allocator) or
 * memory that outlives the request (true_async_persistent_allocator). Each bails out on failure
 * and never returns NULL. */
typedef struct _allocator_s allocator_t;

struct _allocator_s
{
	void *(*m_alloc)(size_t size);
	void *(*m_calloc)(size_t num, size_t size);
	void *(*m_realloc)(void *ptr, size_t size, const size_t old_size);
	void (*m_free)(void *ptr);
};

extern const allocator_t true_async_allocator;
extern const allocator_t true_async_persistent_allocator;

#endif /* TRUE_ASYNC_ALLOCATOR_H */
