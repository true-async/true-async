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
		async_event_callback_t *inline_callback = vector->inline_callback;
		vector->data = safe_emalloc(new_capacity, sizeof(async_event_callback_t *), 0);

		if (vector->length == 1) {
			vector->data[0] = inline_callback;
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

	const uint32_t last_index = --vector->length;

	if (notifying && index < vector->cursor) {
		/* Already run: the last run element takes its place and the last element fills the gap,
		 * so the cursor moves back by one and still points at the first pending callback. */
		const uint32_t last_run_index = --vector->cursor;
		slots[index] = slots[last_run_index];
		slots[last_run_index] = slots[last_index];
	} else {
		slots[index] = slots[last_index];
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
	const bool was_in_scheduler_context = ZEND_ASYNC_IN_SCHEDULER_CONTEXT;
	ZEND_ASYNC_IN_SCHEDULER_CONTEXT = true;

	/* The first callback that throws ends the notify, as in TrueAsync: the callbacks behind it stay
	 * in the vector uncalled and are disposed with it. Callbacks run with no exception pending; the
	 * one thrown is chained over the exception pending at entry. */
	zend_object *saved_exception = NULL;
	async_exception_save_fast(&EG(exception), &saved_exception);

	/* data, length and the cursor are reread every step: a callback may add, remove or grow the
	 * vector. A bailout out of a callback leaves the vector marked, as in TrueAsync: the scheduler's
	 * bailout handling unwinds every unfinished coroutine itself, waiters included (TrueAsync's
	 * bailout_all_coroutines()). */
	while (vector->cursor < vector->length) {
		async_event_callback_t *callback = async_callbacks_slots(vector)[vector->cursor++];
		callback->callback(target, callback, result, exception);

		if (UNEXPECTED(EG(exception) != NULL)) {
			break;
		}
	}

	vector->capacity &= ~ASYNC_CALLBACKS_F_NOTIFYING;
	ZEND_ASYNC_IN_SCHEDULER_CONTEXT = was_in_scheduler_context;

	async_exception_restore_fast(&EG(exception), &saved_exception);
}

void async_callbacks_free(async_awaitable_t *target, async_callbacks_vector_t *vector)
{
	/* A bailout out of a notify left the mark: removals below are plain swaps with the last. */
	vector->capacity &= ~ASYNC_CALLBACKS_F_NOTIFYING;

	uint32_t index = 0;

	while (index < vector->length) {
		async_event_callback_t *callback = async_callbacks_slots(vector)[index];

		/* Detached before the wake, whatever the wake does: the waiter's unlink finds it gone, and the
		 * last element takes its slot. */
		if (UNEXPECTED(callback->flags & ASYNC_CALLBACK_F_RECORD)) {
			async_callbacks_remove(vector, callback);
			((async_coroutine_event_callback_t *) callback)->event = NULL;
			callback->callback(target, callback, NULL, NULL);
			continue;
		}

		if (callback->dispose != NULL) {
			callback->dispose(callback, target);
		}

		index++;
	}

	if (ASYNC_CALLBACKS_CAPACITY(vector) != 0) {
		efree(vector->data);
	}

	vector->inline_callback = NULL;
	vector->length = 0;
	vector->capacity = 0;
	vector->cursor = 0;
}

void async_wait_unlink(async_coroutine_t *coroutine)
{
	async_coroutine_event_callback_t *record = &coroutine->waker.record;
	async_awaitable_t *target = record->event;

	if (EXPECTED(target == NULL)) {
		return;
	}

	/* Only coroutines are awaited until events come (S4). */
	ZEND_ASSERT(ASYNC_AWAITABLE_IS_COROUTINE(target));

	const bool removed = async_callbacks_remove(&((async_coroutine_t *) target)->callbacks, &record->event_callback);
	ZEND_ASSERT(removed && "a linked record is in its target's vector");
	(void) removed;
	record->event = NULL;
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
	finish_handler->event_callback.flags = 0;
	finish_handler->event_callback.callback = async_finish_handler_call;
	finish_handler->event_callback.dispose = async_finish_handler_dispose;
	finish_handler->handler = handler;
	finish_handler->waiter = waiter;
	finish_handler->data = data;

	/* One counter per thread; 0 is the RFC's "nothing added". */
	if (UNEXPECTED(++ASYNC_G(last_finish_handler_id) == 0)) {
		ASYNC_G(last_finish_handler_id) = 1;
	}

	finish_handler->handler_id = ASYNC_G(last_finish_handler_id);
	async_callbacks_push_reserved(vector, &finish_handler->event_callback);

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
