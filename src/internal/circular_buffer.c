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
#include "circular_buffer.h"

#define MINIMUM_COUNT 4

static zend_always_inline size_t round_up_to_power_of_2(const size_t n)
{
	size_t power = 1;

	while (power < n) {
		power <<= 1;
	}

	return power;
}

static zend_always_inline size_t next_index(const size_t index, const size_t capacity)
{
	ZEND_ASSERT((capacity & (capacity - 1)) == 0 && "capacity must be a power of 2");
	return (index + 1) & (capacity - 1);
}

circular_buffer_t *circular_buffer_new(size_t count, const size_t item_size, const allocator_t *allocator)
{
	if (allocator == NULL) {
		allocator = &true_async_allocator;
	}

	circular_buffer_t *buffer = allocator->m_calloc(1, sizeof(circular_buffer_t));
	circular_buffer_ctor(buffer, count, item_size, allocator);

	return buffer;
}

void circular_buffer_destroy(circular_buffer_t *buffer)
{
	buffer->allocator->m_free(buffer->data);
	buffer->allocator->m_free(buffer);
}

zend_result
circular_buffer_ctor(circular_buffer_t *buffer, size_t count, const size_t item_size, const allocator_t *allocator)
{
	ZEND_ASSERT(item_size > 0);

	if (allocator == NULL) {
		allocator = &true_async_allocator;
	}

	count = round_up_to_power_of_2(count == 0 ? MINIMUM_COUNT : count);

	buffer->allocator = allocator;
	buffer->item_size = item_size;
	buffer->min_size = count;
	buffer->capacity = count;
	buffer->auto_optimize = true;
	buffer->decrease_t = count > MINIMUM_COUNT ? (count / 2 - count / 4) : 0;
	buffer->data = allocator->m_calloc(count, item_size);
	buffer->head = 0;
	buffer->tail = 0;

	return SUCCESS;
}

void circular_buffer_dtor(circular_buffer_t *buffer)
{
	if (buffer->data != NULL) {
		buffer->allocator->m_free(buffer->data);
		buffer->data = NULL;
	}
}

static zend_always_inline bool should_decrease(const circular_buffer_t *buffer)
{
	return buffer->auto_optimize && !circular_buffer_is_empty(buffer) &&
			circular_buffer_count(buffer) < buffer->decrease_t;
}

static void recalc_decrease_threshold(circular_buffer_t *buffer, const size_t new_count)
{
	buffer->decrease_t = (new_count <= buffer->min_size) ? 0 : (new_count / 2 - new_count / 4);
}

zend_result circular_buffer_realloc(circular_buffer_t *buffer, size_t new_count)
{
	ZEND_ASSERT(buffer->data != NULL && buffer->capacity > 0);
	ZEND_ASSERT(buffer->head < buffer->capacity && buffer->tail < buffer->capacity);

	if (new_count == 0) {
		if (circular_buffer_is_full(buffer)) {
			new_count = buffer->capacity * 2;
		} else if (should_decrease(buffer)) {
			new_count = MAX(buffer->capacity / 2, buffer->min_size);
		} else {
			return SUCCESS;
		}
	} else {
		new_count = round_up_to_power_of_2(new_count);
	}

	ZEND_ASSERT(new_count >= buffer->min_size && "a buffer never shrinks below its minimum");

	const size_t item_size = buffer->item_size;
	const size_t count = circular_buffer_count(buffer);
	ZEND_ASSERT(count < new_count && "the new capacity must hold every item and the free slot");

	if (count == 0) {
		void *new_data = buffer->allocator->m_alloc(new_count * item_size);
		buffer->allocator->m_free(buffer->data);
		buffer->data = new_data;
		buffer->head = 0;
		buffer->tail = 0;
	} else if (buffer->head >= buffer->tail && new_count > buffer->capacity) {
		/* Contiguous items, growing: they stay where they are. */
		buffer->data = buffer->allocator->m_realloc(buffer->data, new_count * item_size, buffer->capacity * item_size);
	} else if (buffer->head >= buffer->tail) {
		/* Contiguous items, shrinking: [tail, head) moves to the start. */
		void *new_data = buffer->allocator->m_alloc(new_count * item_size);
		memcpy(new_data, (char *) buffer->data + buffer->tail * item_size, count * item_size);
		buffer->allocator->m_free(buffer->data);
		buffer->data = new_data;
		buffer->tail = 0;
		buffer->head = count;
	} else {
		/* Wrapped items: [tail, capacity) then [0, head) go to the start in order. */
		void *new_data = buffer->allocator->m_alloc(new_count * item_size);
		const size_t first_part = buffer->capacity - buffer->tail;
		memcpy(new_data, (char *) buffer->data + buffer->tail * item_size, first_part * item_size);
		memcpy((char *) new_data + first_part * item_size, buffer->data, buffer->head * item_size);
		buffer->allocator->m_free(buffer->data);
		buffer->data = new_data;
		buffer->tail = 0;
		buffer->head = count;
	}

	buffer->capacity = new_count;
	recalc_decrease_threshold(buffer, new_count);

	ZEND_ASSERT(circular_buffer_count(buffer) == count);
	return SUCCESS;
}

/* Grows a full buffer, or shrinks an underused one, before an item goes in. */
static zend_result circular_buffer_resize_for_push(circular_buffer_t *buffer)
{
	if (circular_buffer_is_full(buffer) || should_decrease(buffer)) {
		return circular_buffer_realloc(buffer, 0);
	}

	return SUCCESS;
}

zend_result circular_buffer_push(circular_buffer_t *buffer, const void *value, const bool should_resize)
{
	ZEND_ASSERT(buffer->data != NULL && value != NULL);

	if (should_resize) {
		if (UNEXPECTED(circular_buffer_resize_for_push(buffer) == FAILURE)) {
			return FAILURE;
		}
	} else if (UNEXPECTED(circular_buffer_is_full(buffer))) {
		zend_error(E_WARNING, "Cannot push into full circular buffer");
		return FAILURE;
	}

	memcpy((char *) buffer->data + buffer->head * buffer->item_size, value, buffer->item_size);
	buffer->head = next_index(buffer->head, buffer->capacity);

	return SUCCESS;
}

zend_result circular_buffer_push_front(circular_buffer_t *buffer, const void *value, const bool should_resize)
{
	ZEND_ASSERT(buffer->data != NULL && value != NULL);

	if (should_resize) {
		if (UNEXPECTED(circular_buffer_resize_for_push(buffer) == FAILURE)) {
			return FAILURE;
		}
	} else if (UNEXPECTED(circular_buffer_is_full(buffer))) {
		return circular_buffer_push(buffer, value, false);
	}

	buffer->tail = (buffer->tail == 0 ? buffer->capacity : buffer->tail) - 1;
	memcpy((char *) buffer->data + buffer->tail * buffer->item_size, value, buffer->item_size);

	return SUCCESS;
}

zend_result circular_buffer_pop(circular_buffer_t *buffer, void *value)
{
	ZEND_ASSERT(buffer->data != NULL && value != NULL);

	if (UNEXPECTED(circular_buffer_is_empty(buffer))) {
		zend_error(E_WARNING, "Cannot pop from empty circular buffer");
		return FAILURE;
	}

	memcpy(value, (char *) buffer->data + buffer->tail * buffer->item_size, buffer->item_size);
	buffer->tail = next_index(buffer->tail, buffer->capacity);

	return SUCCESS;
}

bool circular_buffer_is_empty(const circular_buffer_t *buffer)
{
	return buffer->head == buffer->tail;
}

bool circular_buffer_is_full(const circular_buffer_t *buffer)
{
	return next_index(buffer->head, buffer->capacity) == buffer->tail;
}

size_t circular_buffer_count(const circular_buffer_t *buffer)
{
	/* A buffer never constructed (a scheduler queue read before the scheduler allocates it). */
	if (UNEXPECTED(buffer->capacity == 0)) {
		return 0;
	}

	ZEND_ASSERT(buffer->head < buffer->capacity && buffer->tail < buffer->capacity);

	return buffer->head >= buffer->tail ? buffer->head - buffer->tail : buffer->capacity - buffer->tail + buffer->head;
}

size_t circular_buffer_capacity(const circular_buffer_t *buffer)
{
	return buffer->capacity - 1;
}
