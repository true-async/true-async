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

void circular_buffer_ctor(circular_buffer_t *buffer)
{
	buffer->capacity = INITIAL_CAPACITY;
	buffer->data = emalloc(INITIAL_CAPACITY * sizeof(void *));
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

	const size_t capacity = buffer->capacity;

	buffer->data = erealloc2(buffer->data, 2 * capacity * sizeof(void *), capacity * sizeof(void *));

	if (buffer->tail != 0) {
		memcpy(buffer->data + capacity, buffer->data, buffer->head * sizeof(void *));
		buffer->head += capacity;
	}

	buffer->capacity = 2 * capacity;
}

void circular_buffer_push(circular_buffer_t *buffer, void *ptr)
{
	ZEND_ASSERT(buffer->data != NULL);

	if (UNEXPECTED(circular_buffer_is_full(buffer))) {
		circular_buffer_grow(buffer);
	}

	buffer->data[buffer->head] = ptr;
	buffer->head = next_index(buffer->head, buffer->capacity);
}

void circular_buffer_push_front(circular_buffer_t *buffer, void *ptr)
{
	ZEND_ASSERT(buffer->data != NULL);

	if (UNEXPECTED(circular_buffer_is_full(buffer))) {
		circular_buffer_grow(buffer);
	}

	buffer->tail = (buffer->tail == 0 ? buffer->capacity : buffer->tail) - 1;
	buffer->data[buffer->tail] = ptr;
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
	ZEND_ASSERT(buffer->head < buffer->capacity && buffer->tail < buffer->capacity);

	return buffer->head >= buffer->tail ? buffer->head - buffer->tail : buffer->capacity - buffer->tail + buffer->head;
}
