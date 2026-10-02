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

	if (needed <= (capacity == 0 ? 1 : capacity)) {
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
	uint32_t index = vector->length;

	/* A callback removing itself during the notify sits just behind the cursor. */
	if (notifying && vector->cursor > 0 && slots[vector->cursor - 1] == callback) {
		index = vector->cursor - 1;
	} else {
		for (index = 0; index < vector->length && slots[index] != callback; index++) {
		}

		if (index == vector->length) {
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

/* Puts the exceptions back into EG(exception). An exception pending at entry gets its frame back
 * as zend_objects_destroy_object() does around a destructor (zend_objects.c:158-177). */
static void async_callbacks_exception_back(zend_object **pending,
										   zend_execute_data *execute_data,
										   const zend_op *opline_before_exception)
{
	if (execute_data != NULL) {
		execute_data->opline = EG(exception_op);
		EG(opline_before_exception) = opline_before_exception;
	}

	async_exception_restore_fast(&EG(exception), pending);
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
		return true;
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
	const zend_op *opline_before_exception = NULL;
	/* The frame that had an exception pending at entry; NULL without one. */
	zend_execute_data *const execute_data = EG(exception) != NULL ? EG(current_execute_data) : NULL;

	if (execute_data != NULL) {
		if (execute_data->func != NULL && ZEND_USER_CODE(execute_data->func->common.type)) {
			zend_rethrow_exception(execute_data);
		}

		execute_data->opline = EG(opline_before_exception);
		opline_before_exception = EG(opline_before_exception);
	}

	async_exception_save_fast(&EG(exception), &pending);

	/* data, length and the cursor are reread every step: a callback may add, remove, grow, or
	 * free the vector (which restarts the cursor on an empty vector). A bailout out of a callback
	 * leaves the vector marked, as in TrueAsync: the scheduler's bailout handling unwinds every
	 * unfinished coroutine itself, waiters included (TrueAsync's bailout_all_coroutines()). */
	while (vector->cursor < vector->length) {
		async_event_callback_t *callback = async_callbacks_slots(vector)[vector->cursor++];
		callback->callback(target, callback, result, exception);

		if (UNEXPECTED(EG(exception) != NULL)) {
			async_exception_save_fast(&EG(exception), &pending);
		}
	}

	vector->capacity &= ~ASYNC_CALLBACKS_F_NOTIFYING;
	ZEND_ASYNC_IN_SCHEDULER_CONTEXT = in_scheduler_context;

	async_callbacks_exception_back(&pending, execute_data, opline_before_exception);

	return true;
}

void async_callbacks_free(async_awaitable_t *target, async_callbacks_vector_t *vector)
{
	async_event_callback_t **slots = async_callbacks_slots(vector);

	for (uint32_t i = 0; i < vector->length; i++) {
		async_event_callback_t *callback = slots[i];

		if (callback->flags & ASYNC_CALLBACK_F_RECORD) {
			/* Invariant F (section 4): a waiter unlinks before its target goes. Until the teardown
			 * wakes such a waiter with an error (S3.5), its later unlink at least finds no target. */
			ZEND_ASSERT(0 && "a wait record outlived its frame's link");
			((async_coroutine_event_callback_t *) callback)->event = NULL;
		} else if (callback->dispose != NULL) {
			callback->dispose(callback, target);
		}
	}

	if (ASYNC_CALLBACKS_CAPACITY(vector) != 0) {
		efree(vector->data);
	}

	/* Freed by a callback of its own notify: the bit stays until that notify ends, which length 0
	 * makes it do at once. The caller of the notify keeps the owner alive (see the header). */
	vector->single = NULL;
	vector->length = 0;
	vector->capacity &= ASYNC_CALLBACKS_F_NOTIFYING;
	vector->cursor = 0;
}

static void async_finish_handler_call(async_awaitable_t *target,
									  async_event_callback_t *callback,
									  void *result,
									  zend_object *exception)
{
	ZEND_ASSERT(ASYNC_AWAITABLE_IS_COROUTINE(target));

	async_finish_handler_callback_t *entry = (async_finish_handler_callback_t *) callback;
	zend_coroutine_t *coroutine = (zend_coroutine_t *) target;
	const bool is_bailout = (coroutine->flags & ASYNC_COROUTINE_F_BAILOUT) != 0 || ASYNC_G(bailing_out);
	/* Finish handlers are added only to a coroutine of this extension. */
	async_callbacks_vector_t *vector = &((async_coroutine_t *) target)->callbacks;

	/* While the handler runs, a removal by id or the vector's teardown only unlinks the entry;
	 * freeing it stays here. */
	entry->base.flags |= ASYNC_CALLBACK_F_RUNNING;
	const bool keep = entry->handler(coroutine, entry->waiter, entry->data, is_bailout);
	entry->base.flags &= ~ASYNC_CALLBACK_F_RUNNING;

	if (entry->base.flags & ASYNC_CALLBACK_F_REMOVED) {
		efree(entry);
		return;
	}

	if (keep) {
		return;
	}

	const bool removed = async_callbacks_remove(vector, callback);
	ZEND_ASSERT(removed && "a finish handler outside its coroutine's vector");
	(void) removed;
	efree(entry);
}

static void async_finish_handler_dispose(async_event_callback_t *callback, async_awaitable_t *target)
{
	if (callback->flags & ASYNC_CALLBACK_F_RUNNING) {
		callback->flags |= ASYNC_CALLBACK_F_REMOVED;
		return;
	}

	efree(callback);
}

uint32_t async_finish_handler_add(async_coroutine_t *coroutine,
								  const zend_coroutine_finish_handler_fn handler,
								  zend_coroutine_t *waiter,
								  void *data)
{
	async_callbacks_vector_t *vector = &coroutine->callbacks;
	async_callbacks_reserve(vector, 1);

	async_finish_handler_callback_t *entry = emalloc(sizeof(async_finish_handler_callback_t));
	entry->base.ref_count = 1;
	entry->base.flags = 0;
	entry->base.callback = async_finish_handler_call;
	entry->base.dispose = async_finish_handler_dispose;
	entry->handler = handler;
	entry->waiter = waiter;
	entry->data = data;

	/* One counter per thread; 0 is the RFC's "nothing added". */
	if (UNEXPECTED(++ASYNC_G(handler_id_seq) == 0)) {
		ASYNC_G(handler_id_seq) = 1;
	}

	entry->handler_id = ASYNC_G(handler_id_seq);
	async_callbacks_push_reserved(vector, &entry->base);

	return entry->handler_id;
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
			async_finish_handler_dispose(callback, NULL);
			return true;
		}
	}

	return false;
}
