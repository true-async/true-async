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

/* First array of a coroutine's switch handlers. */
#define ASYNC_SWITCH_HANDLERS_FIRST_CAPACITY 4

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

static zend_always_inline void callbacks_slot_update(async_event_callback_t **slots, const uint32_t index)
{
	async_event_callback_t *callback = slots[index];

	if (EXPECTED(!(callback->flags & ASYNC_CALLBACK_F_SHARED))) {
		callback->slot = index;
	}
}

bool async_callbacks_remove(async_callbacks_vector_t *vector, async_event_callback_t *callback)
{
	async_event_callback_t **slots = async_callbacks_slots(vector);
	const bool notifying = (vector->capacity & ASYNC_CALLBACKS_F_NOTIFYING) != 0;
	uint32_t index = callback->slot;

	if (UNEXPECTED((callback->flags & ASYNC_CALLBACK_F_SHARED) || index >= vector->length ||
				   slots[index] != callback)) {
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

		/* The positions coincide when the cursor is at the end or the removed element ran last: a
		 * slot is updated only where an element stays. */
		if (index < last_index) {
			callbacks_slot_update(slots, index);
		}

		if (last_run_index < last_index) {
			callbacks_slot_update(slots, last_run_index);
		}
	} else {
		slots[index] = slots[last_index];

		if (index < last_index) {
			callbacks_slot_update(slots, index);
		}
	}

	return true;
}

bool async_callbacks_notify(async_awaitable_t *target,
							async_callbacks_vector_t *vector,
							void *result,
							zend_object *exception)
{
	if (UNEXPECTED(vector->capacity & ASYNC_CALLBACKS_F_NOTIFYING)) {
		return false;
	}

	if (vector->length == 0) {
		return false;
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
	bool is_record_called = false;

	while (vector->cursor < vector->length) {
		async_event_callback_t *callback = async_callbacks_slots(vector)[vector->cursor++];
		/* Read before the call, which may free a heap subscriber. */
		is_record_called |= (callback->flags & ASYNC_CALLBACK_F_RECORD) != 0;
		callback->callback(target, callback, result, exception);

		if (UNEXPECTED(EG(exception) != NULL)) {
			break;
		}
	}

	vector->capacity &= ~ASYNC_CALLBACKS_F_NOTIFYING;
	ZEND_ASYNC_IN_SCHEDULER_CONTEXT = was_in_scheduler_context;

	async_exception_restore_fast(&EG(exception), &saved_exception);

	return is_record_called;
}

void async_callbacks_free(async_awaitable_t *target, async_callbacks_vector_t *vector)
{
	/* A bailout out of a notify left the mark: removals below are plain swaps with the last. */
	vector->capacity &= ~ASYNC_CALLBACKS_F_NOTIFYING;

	uint32_t index = 0;

	while (index < vector->length) {
		async_event_callback_t *callback = async_callbacks_slots(vector)[index];

		/* Detached before the wake (through its kind's unlink when it has one), whatever the wake does:
		 * the waiter's unlink finds it gone, and the last element takes its slot. */
		if (UNEXPECTED(callback->flags & ASYNC_CALLBACK_F_RECORD)) {
			async_wait_record_unlink((async_coroutine_event_callback_t *) callback);
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

void async_wait_link(async_coroutine_event_callback_t *record,
					 async_coroutine_t *waiter,
					 async_awaitable_t *target,
					 const async_wait_kind_t *kind,
					 const async_event_callback_fn wake)
{
	ZEND_ASSERT(record->event == NULL && "a record links once per wait");

	record->event_callback.flags =
			kind->unlink != NULL ? ASYNC_CALLBACK_F_RECORD | ASYNC_CALLBACK_F_TYPED : ASYNC_CALLBACK_F_RECORD;
	record->event_callback.callback = wake;
	record->event_callback.kind = kind;
	record->coroutine = waiter;
	record->event = target;
	async_callbacks_push_reserved(async_awaitable_callbacks(target), &record->event_callback);
}

void async_wait_record_remove(async_coroutine_event_callback_t *record)
{
	const bool removed = async_callbacks_remove(async_awaitable_callbacks(record->event), &record->event_callback);
	ZEND_ASSERT(removed && "a linked record is in its target's vector");
	(void) removed;
	record->event = NULL;
}

void async_wait_record_unlink(async_coroutine_event_callback_t *record)
{
	async_awaitable_t *target = record->event;

	if (target == NULL) {
		return;
	}

	if (UNEXPECTED(record->event_callback.flags & ASYNC_CALLBACK_F_TYPED)) {
		record->event_callback.kind->unlink(record);
		ZEND_ASSERT(record->event == NULL && "a kind's unlink clears the record's event");
		return;
	}

	async_wait_record_remove(record);
}

void async_wait_unlink_linked(async_coroutine_t *coroutine)
{
	async_waker_t *waker = &coroutine->waker;

	for (uint32_t i = 0; i < ASYNC_WAKER_INLINE_RECORDS; i++) {
		async_wait_record_unlink(&waker->records[i]);
	}

	if (UNEXPECTED(waker->block != NULL)) {
		waker->block->ops->unlink(waker->block);
	}
}

void async_wait_abort(async_coroutine_t *coroutine)
{
	async_waker_t *waker = &coroutine->waker;

	for (uint32_t i = 0; i < ASYNC_WAKER_INLINE_RECORDS; i++) {
		async_coroutine_event_callback_t *record = &waker->records[i];

		if (record->event != NULL && UNEXPECTED(record->event_callback.kind->abort != NULL)) {
			record->event_callback.kind->abort(record);
		}
	}

	async_wait_unlink(coroutine);
}

void async_wait_walk(async_coroutine_t *coroutine,
					 void (*visit)(const async_coroutine_event_callback_t *record, void *arg),
					 void *arg)
{
	const async_waker_t *waker = &coroutine->waker;

	for (uint32_t i = 0; i < ASYNC_WAKER_INLINE_RECORDS; i++) {
		if (waker->records[i].event != NULL) {
			visit(&waker->records[i], arg);
		}
	}

	if (UNEXPECTED(waker->block != NULL)) {
		waker->block->ops->walk(waker->block, visit, arg);
	}
}

async_wait_block_t *async_wait_take_block(async_coroutine_t *coroutine)
{
	async_wait_unlink(coroutine);

	async_wait_block_t *block = coroutine->waker.block;
	coroutine->waker.block = NULL;

	return block;
}

void async_wait_end(async_coroutine_t *coroutine)
{
	async_wait_unlink(coroutine);

	async_wait_block_t *block = coroutine->waker.block;

	if (UNEXPECTED(block != NULL)) {
		coroutine->waker.block = NULL;
		block->ops->release(block);
	}
}

/* The id of a new finish or switch handler: one counter per thread, so an id is never reused while
 * its handler may still be removed; 0 is the RFC's "nothing added". */
static uint32_t next_handler_id(void)
{
	if (UNEXPECTED(++ASYNC_G(last_handler_id) == 0)) {
		ASYNC_G(last_handler_id) = 1;
	}

	return ASYNC_G(last_handler_id);
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

uint32_t async_finish_handler_add(zend_coroutine_t *zend_coroutine,
								  const zend_coroutine_finish_handler_fn handler,
								  zend_coroutine_t *waiter,
								  void *data)
{
	async_callbacks_vector_t *vector = &((async_coroutine_t *) zend_coroutine)->callbacks;
	async_callbacks_reserve(vector, 1);

	async_finish_handler_callback_t *finish_handler = emalloc(sizeof(async_finish_handler_callback_t));
	finish_handler->event_callback.flags = 0;
	finish_handler->event_callback.callback = async_finish_handler_call;
	finish_handler->event_callback.dispose = async_finish_handler_dispose;
	finish_handler->handler = handler;
	finish_handler->waiter = waiter;
	finish_handler->data = data;

	finish_handler->handler_id = next_handler_id();
	async_callbacks_push_reserved(vector, &finish_handler->event_callback);

	return finish_handler->handler_id;
}

bool async_finish_handler_remove(zend_coroutine_t *zend_coroutine, const uint32_t handler_id)
{
	async_callbacks_vector_t *vector = &((async_coroutine_t *) zend_coroutine)->callbacks;
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

///////////////////////////////////////////////////////////////////
/// Switch handlers
///////////////////////////////////////////////////////////////////

uint32_t async_switch_handler_add(zend_coroutine_t *zend_coroutine, const zend_coroutine_switch_handler_fn handler)
{
	async_coroutine_t *coroutine = (async_coroutine_t *) zend_coroutine;
	async_coroutine_switch_handlers_vector_t *vector = coroutine->switch_handlers;

	if (vector == NULL) {
		vector = ecalloc(1, sizeof(async_coroutine_switch_handlers_vector_t));
		coroutine->switch_handlers = vector;
	}

	if (UNEXPECTED(vector->in_execution)) {
		zend_error(E_WARNING, "Cannot add a switch handler while the switch handlers run");
		return 0;
	}

	for (uint32_t i = 0; i < vector->length; i++) {
		if (vector->data[i].handler == handler) {
			return vector->data[i].handler_id;
		}
	}

	if (vector->length == vector->capacity) {
		vector->capacity = vector->capacity == 0 ? ASYNC_SWITCH_HANDLERS_FIRST_CAPACITY : vector->capacity * 2;
		vector->data = safe_erealloc(vector->data, vector->capacity, sizeof(async_switch_handler_t), 0);
	}

	async_switch_handler_t *switch_handler = &vector->data[vector->length++];
	switch_handler->handler = handler;
	switch_handler->handler_id = next_handler_id();

	return switch_handler->handler_id;
}

bool async_switch_handler_remove(zend_coroutine_t *zend_coroutine, const uint32_t handler_id)
{
	async_coroutine_t *coroutine = (async_coroutine_t *) zend_coroutine;
	async_coroutine_switch_handlers_vector_t *vector = coroutine->switch_handlers;

	if (UNEXPECTED(vector == NULL)) {
		return false;
	}

	if (UNEXPECTED(vector->in_execution)) {
		zend_error(E_WARNING, "Cannot remove a switch handler while the switch handlers run");
		return false;
	}

	for (uint32_t i = 0; i < vector->length; i++) {
		if (vector->data[i].handler_id == handler_id) {
			memmove(&vector->data[i], &vector->data[i + 1], (vector->length - i - 1) * sizeof(async_switch_handler_t));
			vector->length--;

			if (vector->length == 0) {
				async_switch_handlers_free(coroutine);
			}

			return true;
		}
	}

	return false;
}

void async_switch_handlers_call(async_coroutine_t *coroutine, const bool is_enter)
{
	async_coroutine_switch_handlers_vector_t *vector = coroutine->switch_handlers;
	uint32_t kept = 0;

	vector->in_execution = true;

	for (uint32_t i = 0; i < vector->length; i++) {
		if (vector->data[i].handler(&coroutine->coroutine, is_enter)) {
			vector->data[kept++] = vector->data[i];
		}
	}

	vector->length = kept;
	vector->in_execution = false;

	/* The core's handlers go at the first leave: the check before the next call stays one NULL test. */
	if (kept == 0) {
		async_switch_handlers_free(coroutine);
	}
}

void async_switch_handlers_free(async_coroutine_t *coroutine)
{
	async_coroutine_switch_handlers_vector_t *vector = coroutine->switch_handlers;

	if (EXPECTED(vector == NULL)) {
		return;
	}

	coroutine->switch_handlers = NULL;

	if (vector->data != NULL) {
		efree(vector->data);
	}

	efree(vector);
}
