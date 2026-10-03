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
#ifndef TRUE_ASYNC_CIRCULAR_BUFFER_H
#define TRUE_ASYNC_CIRCULAR_BUFFER_H

#include "php.h"
#include "allocator.h"

/* A FIFO of fixed-size items with push to either end. One slot stays free to tell full from
 * empty, so a buffer of capacity N holds N - 1 items. A zero-filled buffer, never constructed,
 * accepts count, is_empty, is_not_empty and pop_ptr only. */
typedef struct _circular_buffer_s circular_buffer_t;

struct _circular_buffer_s
{
	size_t item_size;
	size_t min_size;
	size_t capacity; /* a power of 2 */
	/* Halve the buffer on a push once it is used below decrease_t; off for the run queue and the
	 * context pool (dev/plans/S3.md, section 11). */
	bool auto_optimize;
	/* About a quarter of the capacity, recalculated on every resize: 0 once a resize reaches the
	 * minimum size, but the constructor sets it for any count above 4, so the first shrink of such
	 * a buffer reallocates it at its own size. */
	size_t decrease_t;
	const allocator_t *allocator;
	void *data;
	size_t head; /* next slot to write; head == tail when empty, never catches up with tail */
	size_t tail; /* next slot to read */
};

/* count is rounded up to a power of 2; 0 takes the minimum, 4. A NULL allocator is
 * true_async_allocator. */
zend_result
circular_buffer_ctor(circular_buffer_t *buffer, size_t count, const size_t item_size, const allocator_t *allocator);
void circular_buffer_dtor(circular_buffer_t *buffer);
circular_buffer_t *circular_buffer_new(size_t count, const size_t item_size, const allocator_t *allocator);
void circular_buffer_destroy(circular_buffer_t *buffer);

bool circular_buffer_is_full(const circular_buffer_t *buffer);
bool circular_buffer_is_empty(const circular_buffer_t *buffer);
/* should_resize: grow when full (and shrink, with auto_optimize); without it a full buffer is a
 * warning and FAILURE, except that push_front then falls back to push. */
zend_result circular_buffer_push(circular_buffer_t *buffer, const void *value, bool should_resize);
zend_result circular_buffer_push_front(circular_buffer_t *buffer, const void *value, bool should_resize);
/* An empty buffer is a warning and FAILURE. */
zend_result circular_buffer_pop(circular_buffer_t *buffer, void *value);
size_t circular_buffer_count(const circular_buffer_t *buffer);
/* Items it can hold: capacity - 1. */
size_t circular_buffer_capacity(const circular_buffer_t *buffer);
/* Resizes to new_count rounded up to a power of 2, keeping the order; new_count 0 grows a full
 * buffer, shrinks an underused one, or does nothing. */
zend_result circular_buffer_realloc(circular_buffer_t *buffer, size_t new_count);

static zend_always_inline bool circular_buffer_is_not_empty(const circular_buffer_t *buffer)
{
	return buffer->head != buffer->tail;
}

static zend_always_inline void circular_buffer_clean(circular_buffer_t *buffer)
{
	buffer->head = buffer->tail;
}

/* Pointer-sized items, no memcpy. FAILURE when full; nothing grows. */
static zend_always_inline zend_result circular_buffer_push_ptr(circular_buffer_t *buffer, void *ptr)
{
	if (EXPECTED(((buffer->head + 1) & (buffer->capacity - 1)) != buffer->tail)) {
		*(void **) ((char *) buffer->data + buffer->head * sizeof(void *)) = ptr;
		buffer->head = (buffer->head + 1) & (buffer->capacity - 1);
		return SUCCESS;
	}

	return FAILURE;
}

/* FAILURE when empty, without a warning. */
static zend_always_inline zend_result circular_buffer_pop_ptr(circular_buffer_t *buffer, void **ptr)
{
	if (EXPECTED(buffer->head != buffer->tail)) {
		*ptr = *(void **) ((char *) buffer->data + buffer->tail * sizeof(void *));
		buffer->tail = (buffer->tail + 1) & (buffer->capacity - 1);
		return SUCCESS;
	}

	return FAILURE;
}

/* Swaps the pointer-sized items at offsets i and j from the tail, both in [0, count); the
 * scheduler's fuzz hook permutes the run queue with it. */
static zend_always_inline void circular_buffer_swap_ptr_at(circular_buffer_t *buffer, const size_t i, const size_t j)
{
	if (i == j) {
		return;
	}

	const size_t mask = buffer->capacity - 1;
	void **slot_i = (void **) ((char *) buffer->data + ((buffer->tail + i) & mask) * sizeof(void *));
	void **slot_j = (void **) ((char *) buffer->data + ((buffer->tail + j) & mask) * sizeof(void *));
	void *swapped_item = *slot_i;
	*slot_i = *slot_j;
	*slot_j = swapped_item;
}

/* The fast push, growing the buffer when it is full. */
static zend_always_inline zend_result circular_buffer_push_ptr_with_resize(circular_buffer_t *buffer, void *ptr)
{
	if (EXPECTED(circular_buffer_push_ptr(buffer, ptr) == SUCCESS)) {
		return SUCCESS;
	}

	return circular_buffer_push(buffer, &ptr, true);
}

#endif /* TRUE_ASYNC_CIRCULAR_BUFFER_H */
