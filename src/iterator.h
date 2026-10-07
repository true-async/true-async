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
#ifndef TRUE_ASYNC_ITERATOR_H
#define TRUE_ASYNC_ITERATOR_H

#include "php.h"
#include "true_async_API.h"

/* TrueAsync's iterator core (iterator.c, dev/plans/S9-scope.md 7): a handler runs for each element of
 * an array or a Traversable in worker coroutines of the iterator's scope. A worker walks the elements
 * one after another; while a handler waits, the microtask spawns another worker, up to `concurrency`
 * (0: no limit), which goes on from the shared position. The finally handlers of a scope and of a
 * coroutine run on it; iterate() will. */

typedef struct _async_iterator_s async_iterator_t;

/* Called for an element; FAILURE or an exception stops the walk. */
typedef zend_result (*async_iterator_handler_t)(async_iterator_t *iterator, zval *current, zval *key);
typedef void (*async_iterator_method_t)(async_iterator_t *iterator);

typedef enum
{
	ASYNC_ITERATOR_INIT = 0,
	/* A worker moves the Traversable, which may suspend: no other worker may read it meanwhile. */
	ASYNC_ITERATOR_MOVING,
	ASYNC_ITERATOR_STARTED,
	ASYNC_ITERATOR_FINISHED,
} async_iterator_state_t;

struct _async_iterator_s
{
	/* First: the microtask's last release frees the iterator. Each worker and the queued microtask hold a
	 * reference, and so does the creator until it starts the walk. */
	zend_async_microtask_t microtask;
	async_scope_t *scope;                  /* where the workers are spawned */
	async_iterator_method_t extended_dtor; /* releases what a caller's larger struct holds */
	unsigned int concurrency;
	bool is_hi_priority; /* each worker goes to the front of the queue on its first enqueue */
	async_iterator_state_t state;
	/* Workers that have not left, counted when spawned, so the concurrency gate never admits one past the
	 * limit while another waits for its first run. */
	unsigned int active_coroutines;
	/* What stopped the walk, or what an owner's handler left; the worker that leaves last ends with it,
	 * where TrueAsync notifies a completion event. A cancellation or an exit stays the worker's. */
	zend_object *exception;
	async_iterator_handler_t handler;
	zend_fcall_t *fcall; /* a callable called as fn($value, $key) instead of `handler`; the iterator's */
	zval array;          /* a copy of the walked array, UNDEF for a Traversable */
	HashTable *target_hash;
	HashPosition position;
	uint32_t hash_iterator;              /* the engine's iterator of `array`, kept valid across its changes */
	zend_object_iterator *zend_iterator; /* the walked Traversable's, the iterator's */
};

/* An iterator over `array` (copied, never immutable: the caller separates it) or over `zend_iterator`
 * (taken), calling `fcall` (taken) or `handler` for each element, with workers spawned in `scope`.
 * `iterator_size` is the size of a caller's struct that starts with this one, or 0. Nothing runs
 * until async_iterator_run() or async_iterator_run_in_coroutine(). */
async_iterator_t *async_iterator_new(zval *array,
									 zend_object_iterator *zend_iterator,
									 zend_fcall_t *fcall,
									 async_iterator_handler_t handler,
									 async_scope_t *scope,
									 unsigned int concurrency,
									 bool is_hi_priority,
									 size_t iterator_size);

/* Walks the elements in the current coroutine, with workers joining while a handler waits. The
 * walk's exception lands in `exception` and cancels the iterator's scope; the current coroutine is no
 * worker and does not end with it. */
void async_iterator_run(async_iterator_t *iterator);

/* Walks the elements in a new worker, which takes the creator's reference. False when the worker
 * cannot be spawned; the reference is the caller's then. */
bool async_iterator_run_in_coroutine(async_iterator_t *iterator);

#endif /* TRUE_ASYNC_ITERATOR_H */
