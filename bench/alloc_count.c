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

/*
 * An LD_PRELOAD allocation counter for the benchmarks (dev/plans/S3.md, section 12). Run PHP with
 * USE_ZEND_ALLOC=0 so emalloc goes to malloc; at exit it prints the number of malloc, calloc,
 * realloc of NULL, posix_memalign, aligned_alloc and memalign calls to stderr.
 * Build: cc -O2 -shared -fPIC -o bench/alloc_count.so bench/alloc_count.c -ldl
 */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static unsigned long allocation_count;
static void *(*real_malloc)(size_t);
static void *(*real_calloc)(size_t, size_t);
static void *(*real_realloc)(void *, size_t);
static int (*real_posix_memalign)(void **, size_t, size_t);
static void *(*real_aligned_alloc)(size_t, size_t);
static void *(*real_memalign)(size_t, size_t);

/* dlsym itself may call calloc before real_calloc is known: serve it from a static buffer. */
static char bootstrap_buffer[4096];
static size_t bootstrap_used;

static void resolve(void)
{
	real_malloc = dlsym(RTLD_NEXT, "malloc");
	real_calloc = dlsym(RTLD_NEXT, "calloc");
	real_realloc = dlsym(RTLD_NEXT, "realloc");
	real_posix_memalign = dlsym(RTLD_NEXT, "posix_memalign");
	real_aligned_alloc = dlsym(RTLD_NEXT, "aligned_alloc");
	real_memalign = dlsym(RTLD_NEXT, "memalign");
}

void *malloc(size_t size)
{
	if (real_malloc == NULL) {
		resolve();
	}

	__atomic_add_fetch(&allocation_count, 1, __ATOMIC_RELAXED);
	return real_malloc(size);
}

void *calloc(size_t count, size_t size)
{
	if (real_calloc == NULL) {
		size_t bytes = (count * size + 15) & ~(size_t) 15;
		void *block = bootstrap_buffer + bootstrap_used;

		bootstrap_used += bytes;
		memset(block, 0, bytes);
		return block;
	}

	__atomic_add_fetch(&allocation_count, 1, __ATOMIC_RELAXED);
	return real_calloc(count, size);
}

void *realloc(void *block, size_t size)
{
	if (real_realloc == NULL) {
		resolve();
	}

	if (block == NULL) {
		__atomic_add_fetch(&allocation_count, 1, __ATOMIC_RELAXED);
	}

	return real_realloc(block, size);
}

void free(void *block)
{
	static void (*real_free)(void *);

	if ((char *) block >= bootstrap_buffer && (char *) block < bootstrap_buffer + sizeof(bootstrap_buffer)) {
		return;
	}

	if (real_free == NULL) {
		real_free = dlsym(RTLD_NEXT, "free");
	}

	real_free(block);
}

int posix_memalign(void **result, size_t alignment, size_t size)
{
	if (real_posix_memalign == NULL) {
		resolve();
	}

	__atomic_add_fetch(&allocation_count, 1, __ATOMIC_RELAXED);
	return real_posix_memalign(result, alignment, size);
}

void *aligned_alloc(size_t alignment, size_t size)
{
	if (real_aligned_alloc == NULL) {
		resolve();
	}

	__atomic_add_fetch(&allocation_count, 1, __ATOMIC_RELAXED);
	return real_aligned_alloc(alignment, size);
}

void *memalign(size_t alignment, size_t size)
{
	if (real_memalign == NULL) {
		resolve();
	}

	__atomic_add_fetch(&allocation_count, 1, __ATOMIC_RELAXED);
	return real_memalign(alignment, size);
}

__attribute__((destructor)) static void report(void)
{
	char line[64];
	int length = snprintf(line, sizeof(line), "allocations: %lu\n", allocation_count);

	if (write(STDERR_FILENO, line, length) < 0) {
		return;
	}
}
