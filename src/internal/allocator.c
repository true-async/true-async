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
#include "allocator.h"

#include "php.h"

static void *true_async_alloc(const size_t size)
{
	return emalloc(size);
}

static void *true_async_calloc(const size_t num, const size_t size)
{
	return ecalloc(num, size);
}

static void *true_async_realloc(void *ptr, const size_t size, const size_t old_size)
{
	return erealloc2(ptr, size, old_size);
}

static void true_async_free(void *ptr)
{
	efree(ptr);
}

const allocator_t true_async_allocator = { true_async_alloc, true_async_calloc, true_async_realloc, true_async_free };

static void *true_async_palloc(const size_t size)
{
	return pemalloc(size, 1);
}

static void *true_async_pcalloc(const size_t num, const size_t size)
{
	return pecalloc(num, size, 1);
}

static void *true_async_prealloc(void *ptr, const size_t size, const size_t old_size)
{
	return perealloc2(ptr, size, old_size, 1);
}

static void true_async_pfree(void *ptr)
{
	pefree(ptr, 1);
}

const allocator_t true_async_persistent_allocator = {
	true_async_palloc, true_async_pcalloc, true_async_prealloc, true_async_pfree
};
