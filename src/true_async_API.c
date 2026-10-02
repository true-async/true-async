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
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "php.h"
#include "php_true_async.h"
#include "true_async_API.h"
#include "coroutine.h"

/* First heap array of a vector that outgrows its inline element: 4 slots, 32 B. */
#define ASYNC_CALLBACKS_FIRST_CAPACITY 4

void async_callbacks_reserve(async_callbacks_vector_t *vector, const uint32_t count)
{
	const uint32_t capacity = ASYNC_CALLBACKS_CAPACITY(vector);
	const uint32_t needed = vector->length + count;

	if (EXPECTED(needed <= (capacity == 0 ? 1 : capacity))) {
		return;
	}

	uint32_t new_capacity = capacity == 0 ? ASYNC_CALLBACKS_FIRST_CAPACITY : capacity * 2;

	while (new_capacity < needed) {
		new_capacity *= 2;
	}

	if (capacity == 0) {
		async_event_callback_t *single = vector->single;
		vector->data = safe_emalloc(new_capacity, sizeof(async_event_callback_t *), 0);

		if (vector->length == 1) {
			vector->data[0] = single;
		}
	} else {
		vector->data = safe_erealloc(vector->data, new_capacity, sizeof(async_event_callback_t *), 0);
	}

	vector->capacity = new_capacity | (vector->capacity & ASYNC_CALLBACKS_F_NOTIFYING);
}

bool async_callbacks_remove(async_callbacks_vector_t *vector, async_event_callback_t *callback)
{
	async_event_callback_t **slots = async_callbacks_slots(vector);
	const bool notifying = (vector->capacity & ASYNC_CALLBACKS_F_NOTIFYING) != 0;
	uint32_t index;

	/* A callback removing itself during the notify sits just behind the cursor. */
	if (notifying && vector->cursor > 0 && slots[vector->cursor - 1] == callback) {
		index = vector->cursor - 1;
	} else {
		for (index = 0; index < vector->length && slots[index] != callback; index++) {
		}

		if (UNEXPECTED(index == vector->length)) {
			return false;
		}
	}

	const uint32_t last = --vector->length;

	if (notifying && index < vector->cursor) {
		/* Already run: the last run element takes its place and the last element fills the gap,
		 * so the cursor moves back by one and still points at the first pending callback. */
		const uint32_t last_run = --vector->cursor;
		slots[index] = slots[last_run];
		slots[last_run] = slots[last];
	} else {
		slots[index] = slots[last];
	}

	return true;
}

void async_callbacks_notify(async_awaitable_t *target,
							async_callbacks_vector_t *vector,
							void *result,
							zend_object *exception)
{
	if (UNEXPECTED(vector->capacity & ASYNC_CALLBACKS_F_NOTIFYING) || vector->length == 0) {
		return;
	}

	vector->capacity |= ASYNC_CALLBACKS_F_NOTIFYING;
	vector->cursor = 0;

	/* Callbacks run in scheduler context (S3.md 4.6): suspend and the Fiber methods refuse there,
	 * and a GC run defers to the next tick instead of parking the notify halfway. */
	const bool in_scheduler_context = ZEND_ASYNC_IN_SCHEDULER_CONTEXT;
	ZEND_ASYNC_IN_SCHEDULER_CONTEXT = true;

	/* Every callback runs, whatever an earlier one threw: a finish handler fires exactly once
	 * (zend_async_API.h), and a waiter behind a throwing callback must still wake. Callbacks run
	 * with no exception pending; what they throw is chained, the latest on top, over the exception
	 * pending at entry. */
	zend_object *pending = NULL;
	async_exception_save_fast(&EG(exception), &pending);

	/* data, length and the cursor are reread every step: a callback may add, remove or grow the
	 * vector. A bailout out of a callback leaves the vector marked, as in TrueAsync: the scheduler's
	 * bailout handling unwinds every unfinished coroutine itself, waiters included (TrueAsync's
	 * bailout_all_coroutines()). */
	while (vector->cursor < vector->length) {
		async_event_callback_t *callback = async_callbacks_slots(vector)[vector->cursor++];
		callback->callback(target, callback, result, exception);

		if (UNEXPECTED(EG(exception) != NULL)) {
			async_exception_save_fast(&EG(exception), &pending);
		}
	}

	vector->capacity &= ~ASYNC_CALLBACKS_F_NOTIFYING;
	ZEND_ASYNC_IN_SCHEDULER_CONTEXT = in_scheduler_context;

	async_exception_restore_fast(&EG(exception), &pending);
}

void async_callbacks_free(async_awaitable_t *target, async_callbacks_vector_t *vector)
{
	async_event_callback_t **slots = async_callbacks_slots(vector);

	for (uint32_t i = 0; i < vector->length; i++) {
		async_event_callback_t *callback = slots[i];

		if (UNEXPECTED(callback->flags & ASYNC_CALLBACK_F_RECORD)) {
			/* Invariant F (section 4): a waiter unlinks before its target goes. Until the teardown
			 * wakes such a waiter with an error (S3.7), its later unlink at least finds no target. */
			ZEND_ASSERT(0 && "a wait record outlived its frame's link");
			((async_coroutine_event_callback_t *) callback)->event = NULL;
		} else if (callback->dispose != NULL) {
			callback->dispose(callback, target);
		}
	}

	if (ASYNC_CALLBACKS_CAPACITY(vector) != 0) {
		efree(vector->data);
	}

	vector->single = NULL;
	vector->length = 0;
	vector->capacity = 0;
	vector->cursor = 0;
}

static void async_finish_handler_call(async_awaitable_t *target,
									  async_event_callback_t *callback,
									  void *result,
									  zend_object *exception)
{
	ZEND_ASSERT(ASYNC_AWAITABLE_IS_COROUTINE(target));

	async_finish_handler_callback_t *finish_handler = (async_finish_handler_callback_t *) callback;
	zend_coroutine_t *coroutine = (zend_coroutine_t *) target;
	const zend_coroutine_finish_handler_fn handler = finish_handler->handler;
	zend_coroutine_t *waiter = finish_handler->waiter;
	void *data = finish_handler->data;

	/* Fires once: the entry goes before the handler runs, so the handler may add or remove others.
	 * Finish handlers are added only to a coroutine of this extension. */
	const bool removed = async_callbacks_remove(&((async_coroutine_t *) target)->callbacks, callback);
	ZEND_ASSERT(removed && "a finish handler outside its coroutine's vector");
	(void) removed;
	efree(finish_handler);

	handler(coroutine, waiter, data, (coroutine->flags & ASYNC_COROUTINE_F_BAILOUT) != 0);
}

static void async_finish_handler_dispose(async_event_callback_t *callback, async_awaitable_t *target)
{
	(void) target;
	efree(callback);
}

uint32_t async_finish_handler_add(async_coroutine_t *coroutine,
								  const zend_coroutine_finish_handler_fn handler,
								  zend_coroutine_t *waiter,
								  void *data)
{
	async_callbacks_vector_t *vector = &coroutine->callbacks;
	async_callbacks_reserve(vector, 1);

	async_finish_handler_callback_t *finish_handler = emalloc(sizeof(async_finish_handler_callback_t));
	finish_handler->base.flags = 0;
	finish_handler->base.callback = async_finish_handler_call;
	finish_handler->base.dispose = async_finish_handler_dispose;
	finish_handler->handler = handler;
	finish_handler->waiter = waiter;
	finish_handler->data = data;

	/* One counter per thread; 0 is the RFC's "nothing added". */
	if (UNEXPECTED(++ASYNC_G(handler_id_seq) == 0)) {
		ASYNC_G(handler_id_seq) = 1;
	}

	finish_handler->handler_id = ASYNC_G(handler_id_seq);
	async_callbacks_push_reserved(vector, &finish_handler->base);

	return finish_handler->handler_id;
}

bool async_finish_handler_remove(async_coroutine_t *coroutine, const uint32_t handler_id)
{
	async_callbacks_vector_t *vector = &coroutine->callbacks;
	async_event_callback_t **slots = async_callbacks_slots(vector);

	for (uint32_t i = 0; i < vector->length; i++) {
		async_event_callback_t *callback = slots[i];

		if (callback->callback == async_finish_handler_call &&
			((async_finish_handler_callback_t *) callback)->handler_id == handler_id) {
			async_callbacks_remove(vector, callback);
			efree(callback);
			return true;
		}
	}

	return false;
}
