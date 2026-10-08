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
#include "zval_circular_buffer.h"

#define ZVAL_CIRCULAR_BUFFER_FIRST_SIZE 4

/* Moves the values, oldest first, into a new array of the next size: double, at most `limit`. */
static void zval_circular_buffer_grow(zval_circular_buffer_t *buffer, const uint32_t limit)
{
	ZEND_ASSERT(buffer->size < limit);

	const uint32_t doubled = buffer->size == 0 ? ZVAL_CIRCULAR_BUFFER_FIRST_SIZE : buffer->size * 2;
	const uint32_t new_size = doubled < limit ? doubled : limit;
	zval *const data = safe_emalloc(new_size, sizeof(zval), 0);

	for (uint32_t i = 0; i < buffer->count; i++) {
		ZVAL_COPY_VALUE(&data[i], zval_circular_buffer_at(buffer, i));
	}

	if (buffer->data != NULL) {
		efree(buffer->data);
	}

	buffer->data = data;
	buffer->size = new_size;
	buffer->head = 0;
}

void zval_circular_buffer_push(zval_circular_buffer_t *buffer, const zval *value, const uint32_t limit)
{
	ZEND_ASSERT(buffer->count < limit);

	if (UNEXPECTED(buffer->count == buffer->size)) {
		zval_circular_buffer_grow(buffer, limit);
	}

	ZVAL_COPY(zval_circular_buffer_slot(buffer, buffer->count), value);
	buffer->count++;
}

void zval_circular_buffer_pop(zval_circular_buffer_t *buffer, zval *result)
{
	ZEND_ASSERT(buffer->count > 0);

	ZVAL_COPY_VALUE(result, &buffer->data[buffer->head]);
	buffer->head = buffer->head + 1 < buffer->size ? buffer->head + 1 : 0;
	buffer->count--;
}

void zval_circular_buffer_dtor(zval_circular_buffer_t *buffer)
{
	zval value;

	while (buffer->count > 0) {
		zval_circular_buffer_pop(buffer, &value);
		zval_ptr_dtor(&value);
	}

	if (buffer->data != NULL) {
		efree(buffer->data);
	}

	memset(buffer, 0, sizeof(*buffer));
}
