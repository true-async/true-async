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

#define INITIAL_CAPACITY 4

static zend_always_inline size_t next_index(const size_t index, const size_t capacity)
{
	ZEND_ASSERT((capacity & (capacity - 1)) == 0 && "capacity must be a power of 2");
	return (index + 1) & (capacity - 1);
}

void circular_buffer_ctor(circular_buffer_t *buffer, const size_t item_size)
{
	ZEND_ASSERT(item_size > 0);

	buffer->item_size = item_size;
	buffer->capacity = INITIAL_CAPACITY;
	buffer->data = emalloc(INITIAL_CAPACITY * item_size);
	buffer->head = 0;
	buffer->tail = 0;
}

void circular_buffer_dtor(circular_buffer_t *buffer)
{
	efree(buffer->data);
}

/* Doubles a full buffer, keeping the order. A full buffer is contiguous only from tail 0; otherwise
 * its wrapped items [0, head) move behind [tail, capacity). */
static void circular_buffer_grow(circular_buffer_t *buffer)
{
	ZEND_ASSERT(circular_buffer_is_full(buffer));

	const size_t item_size = buffer->item_size;
	const size_t capacity = buffer->capacity;

	buffer->data = erealloc2(buffer->data, 2 * capacity * item_size, capacity * item_size);

	if (buffer->tail != 0) {
		memcpy((char *) buffer->data + capacity * item_size, buffer->data, buffer->head * item_size);
		buffer->head += capacity;
	}

	buffer->capacity = 2 * capacity;
}

void circular_buffer_push(circular_buffer_t *buffer, const void *value)
{
	ZEND_ASSERT(buffer->data != NULL && value != NULL);

	if (UNEXPECTED(circular_buffer_is_full(buffer))) {
		circular_buffer_grow(buffer);
	}

	memcpy((char *) buffer->data + buffer->head * buffer->item_size, value, buffer->item_size);
	buffer->head = next_index(buffer->head, buffer->capacity);
}

void circular_buffer_push_front(circular_buffer_t *buffer, const void *value)
{
	ZEND_ASSERT(buffer->data != NULL && value != NULL);

	if (UNEXPECTED(circular_buffer_is_full(buffer))) {
		circular_buffer_grow(buffer);
	}

	buffer->tail = (buffer->tail == 0 ? buffer->capacity : buffer->tail) - 1;
	memcpy((char *) buffer->data + buffer->tail * buffer->item_size, value, buffer->item_size);
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
	/* A buffer never constructed (all zero, as a scheduler global before RINIT) holds nothing. */
	if (UNEXPECTED(buffer->capacity == 0)) {
		return 0;
	}

	ZEND_ASSERT(buffer->head < buffer->capacity && buffer->tail < buffer->capacity);

	return buffer->head >= buffer->tail ? buffer->head - buffer->tail : buffer->capacity - buffer->tail + buffer->head;
}
