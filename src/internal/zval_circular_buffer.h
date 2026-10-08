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
#ifndef TRUE_ASYNC_ZVAL_CIRCULAR_BUFFER_H
#define TRUE_ASYNC_ZVAL_CIRCULAR_BUFFER_H

#include "php.h"

/* A FIFO of zvals holding one reference each: a channel's buffer. It allocates nothing until the first
 * push and then doubles when full up to the limit its owner passes, so a large limit costs memory only
 * as values arrive; it never shrinks. Zeroed memory is an empty buffer. */
typedef struct
{
	zval *data;     /* NULL until the first push */
	uint32_t size;  /* slots allocated */
	uint32_t head;  /* slot of the oldest value */
	uint32_t count; /* values held */
} zval_circular_buffer_t;

/* Appends `value`, adding a reference; the caller keeps the count below `limit`. */
void zval_circular_buffer_push(zval_circular_buffer_t *buffer, const zval *value, uint32_t limit);

/* Moves the oldest value into `result`; the buffer is not empty. */
void zval_circular_buffer_pop(zval_circular_buffer_t *buffer, zval *result);

/* Releases the values, oldest first, and frees the slots; the buffer is empty again. */
void zval_circular_buffer_dtor(zval_circular_buffer_t *buffer);

/* The slot `index` places after the oldest value, index < size. */
static zend_always_inline zval *zval_circular_buffer_slot(const zval_circular_buffer_t *buffer, const uint32_t index)
{
	const uint32_t slot = buffer->head + index;

	return &buffer->data[slot < buffer->size ? slot : slot - buffer->size];
}

/* The value `index` places after the oldest, index < count. */
static zend_always_inline zval *zval_circular_buffer_at(const zval_circular_buffer_t *buffer, const uint32_t index)
{
	ZEND_ASSERT(index < buffer->count);

	return zval_circular_buffer_slot(buffer, index);
}

#endif /* TRUE_ASYNC_ZVAL_CIRCULAR_BUFFER_H */
