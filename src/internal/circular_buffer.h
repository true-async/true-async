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

/* A FIFO of pointers with push to either end, growing by doubling when full and never shrinking.
 * One slot stays free to tell full from empty, so a buffer of capacity N holds N - 1 pointers. */
typedef struct _circular_buffer_s circular_buffer_t;

struct _circular_buffer_s
{
	size_t capacity; /* a power of 2 */
	void **data;
	size_t head; /* next slot to write; head == tail when empty, never catches up with tail */
	size_t tail; /* next slot to read */
};

/* Four slots of request memory. */
void circular_buffer_ctor(circular_buffer_t *buffer);
void circular_buffer_dtor(circular_buffer_t *buffer);

bool circular_buffer_is_full(const circular_buffer_t *buffer);
bool circular_buffer_is_empty(const circular_buffer_t *buffer);
/* Both grow a full buffer first. */
void circular_buffer_push(circular_buffer_t *buffer, void *ptr);
void circular_buffer_push_front(circular_buffer_t *buffer, void *ptr);
size_t circular_buffer_count(const circular_buffer_t *buffer);

static zend_always_inline bool circular_buffer_is_not_empty(const circular_buffer_t *buffer)
{
	return buffer->head != buffer->tail;
}

/* Drops every item unread. */
static zend_always_inline void circular_buffer_clean(circular_buffer_t *buffer)
{
	buffer->head = buffer->tail;
}

/* FAILURE when full; nothing grows. */
static zend_always_inline zend_result circular_buffer_push_ptr(circular_buffer_t *buffer, void *ptr)
{
	if (EXPECTED(((buffer->head + 1) & (buffer->capacity - 1)) != buffer->tail)) {
		buffer->data[buffer->head] = ptr;
		buffer->head = (buffer->head + 1) & (buffer->capacity - 1);
		return SUCCESS;
	}

	return FAILURE;
}

/* FAILURE when empty. */
static zend_always_inline zend_result circular_buffer_pop_ptr(circular_buffer_t *buffer, void **ptr)
{
	if (EXPECTED(buffer->head != buffer->tail)) {
		*ptr = buffer->data[buffer->tail];
		buffer->tail = (buffer->tail + 1) & (buffer->capacity - 1);
		return SUCCESS;
	}

	return FAILURE;
}

/* Swaps the pointers at offsets i and j from the tail, both in [0, count); the
 * scheduler's fuzz hook permutes the run queue with it. */
static zend_always_inline void circular_buffer_swap_ptr_at(circular_buffer_t *buffer, const size_t i, const size_t j)
{
	if (i == j) {
		return;
	}

	const size_t mask = buffer->capacity - 1;
	void **slot_i = &buffer->data[(buffer->tail + i) & mask];
	void **slot_j = &buffer->data[(buffer->tail + j) & mask];
	void *swapped_item = *slot_i;
	*slot_i = *slot_j;
	*slot_j = swapped_item;
}

/* The fast push, growing the buffer when it is full. */
static zend_always_inline void circular_buffer_push_ptr_with_resize(circular_buffer_t *buffer, void *ptr)
{
	if (UNEXPECTED(circular_buffer_push_ptr(buffer, ptr) == FAILURE)) {
		circular_buffer_push(buffer, ptr);
	}
}

#endif /* TRUE_ASYNC_CIRCULAR_BUFFER_H */
