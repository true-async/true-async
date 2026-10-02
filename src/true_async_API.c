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
#include "Zend/zend_fibers.h"

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

/* The frame of the notify iterating `vector`; NULL when none is (a bit left by a cut-short notify
 * that async_callbacks_bailout_reset() has not cleared yet). */
static async_notify_frame_t *async_notify_frame_of(const async_callbacks_vector_t *vector)
{
	for (uint32_t depth = ASYNC_G(notify_depth); depth > 0; depth--) {
		async_notify_frame_t *frame = &ASYNC_G(notify_stack)[depth - 1];

		if (frame->vector == vector) {
			return frame;
		}
	}

	return NULL;
}

/* Drops the frames above `depth`, left by notifies a bailout cut short, with the bit and the fiber
 * switch block each of them set. */
static void async_notify_drop_frames(const uint32_t depth)
{
	for (uint32_t top = ASYNC_G(notify_depth); top > depth; top--) {
		async_callbacks_vector_t *vector = ASYNC_G(notify_stack)[top - 1].vector;

		if (vector != NULL) {
			vector->capacity &= ~ASYNC_CALLBACKS_F_NOTIFYING;
		}

		/* The engine resets the counter only at request start. */
		if (zend_fiber_switch_blocked()) {
			zend_fiber_switch_unblock();
		}
	}

	ASYNC_G(notify_depth) = depth;
}

bool async_callbacks_remove(async_callbacks_vector_t *vector, async_event_callback_t *callback)
{
	async_event_callback_t **slots = async_callbacks_slots(vector);
	uint32_t *cursor = NULL;
	uint32_t index = vector->length;

	if (vector->capacity & ASYNC_CALLBACKS_F_NOTIFYING) {
		async_notify_frame_t *frame = async_notify_frame_of(vector);
		ZEND_ASSERT(frame != NULL && "a notify bit without its frame");

		if (EXPECTED(frame != NULL)) {
			cursor = &frame->cursor;

			/* A callback removing itself sits just behind the cursor. */
			if (*cursor > 0 && slots[*cursor - 1] == callback) {
				index = *cursor - 1;
			}
		}
	}

	if (index == vector->length) {
		for (index = 0; index < vector->length && slots[index] != callback; index++) {
		}

		if (index == vector->length) {
			return false;
		}
	}

	const uint32_t last = --vector->length;

	if (cursor != NULL && index < *cursor) {
		/* Already run: the last run element takes its place and the last element fills the gap,
		 * so the cursor moves back by one and still points at the first pending callback. */
		const uint32_t last_run = --*cursor;
		slots[index] = slots[last_run];
		slots[last_run] = slots[last];
	} else {
		slots[index] = slots[last];
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
		return true;
	}

	const uint32_t depth = ASYNC_G(notify_depth);

	if (UNEXPECTED(depth == ASYNC_NOTIFY_DEPTH_MAX)) {
		ZEND_ASSERT(0 && "notify nesting deeper than ASYNC_NOTIFY_DEPTH_MAX");
		return false;
	}

	/* The frame lives in the globals, not on this C stack, so a bailout out of a callback leaves
	 * nothing dangling: async_callbacks_bailout_reset() can still read it. */
	async_notify_frame_t *frame = &ASYNC_G(notify_stack)[depth];
	frame->vector = vector;
	frame->cursor = 0;
	ASYNC_G(notify_depth) = depth + 1;
	vector->capacity |= ASYNC_CALLBACKS_F_NOTIFYING;

	/* Frames are LIFO only while no callback switches fibers: a GC run in a callback would
	 * otherwise park this coroutine mid-notify and let another one's notify reuse the frames. With
	 * switching blocked, the GC defers its run. */
	zend_fiber_switch_block();

	/* Every callback runs, whatever an earlier one threw: a finish handler fires exactly once
	 * (zend_async_API.h), and a waiter behind a throwing callback must still wake. Callbacks run
	 * with no exception pending; what they throw is chained, the latest on top, over the exception
	 * pending at entry. */
	zend_object *pending = NULL;
	const zend_op *opline_before_exception = EG(opline_before_exception);
	const bool entered_with_exception = EG(exception) != NULL;
	async_exception_save_fast(&EG(exception), &pending);

	/* data, length and the cursor are reread every step: a callback may add, remove or grow. */
	while (frame->vector != NULL && frame->cursor < vector->length) {
		async_event_callback_t *callback = async_callbacks_slots(vector)[frame->cursor++];
		callback->callback(target, callback, result, exception);

		if (UNEXPECTED(ASYNC_G(notify_depth) != depth + 1)) {
			async_notify_drop_frames(depth + 1);
		}

		if (UNEXPECTED(EG(exception) != NULL)) {
			async_exception_save_fast(&EG(exception), &pending);
		}
	}

	zend_fiber_switch_unblock();

	/* NULL: the vector was freed by its own notify (async_callbacks_free()). */
	if (frame->vector != NULL) {
		vector->capacity &= ~ASYNC_CALLBACKS_F_NOTIFYING;
	}

	ASYNC_G(notify_depth) = depth;

	/* The VM resumes the entry exception from where it was raised; a callback's throw while it was
	 * aside overwrote the opline. */
	if (entered_with_exception) {
		EG(opline_before_exception) = opline_before_exception;
	}

	async_exception_restore_fast(&EG(exception), &pending);

	return true;
}

void async_callbacks_free(async_awaitable_t *target, async_callbacks_vector_t *vector)
{
	if (UNEXPECTED(vector->capacity & ASYNC_CALLBACKS_F_NOTIFYING)) {
		/* Freed by a callback of its own notify, or after a bailout cut that notify short: the frame
		 * forgets the vector, so neither the notify nor async_callbacks_bailout_reset() touches it. */
		async_notify_frame_t *frame = async_notify_frame_of(vector);

		if (frame != NULL) {
			frame->vector = NULL;
		}
	}

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

	vector->single = NULL;
	vector->length = 0;
	vector->capacity = 0;
}

void async_callbacks_bailout_reset(void)
{
	async_notify_drop_frames(0);
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
	/* The notify running this entry; frames above it, if any, are gone when the handler returns. */
	async_notify_frame_t *frame = &ASYNC_G(notify_stack)[ASYNC_G(notify_depth) - 1];

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

	ZEND_ASSERT(frame->vector != NULL);
	async_callbacks_remove(frame->vector, callback);
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

uint32_t async_finish_handler_add(async_callbacks_vector_t *vector,
								  const zend_coroutine_finish_handler_fn handler,
								  zend_coroutine_t *waiter,
								  void *data)
{
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

bool async_finish_handler_remove(async_callbacks_vector_t *vector, const uint32_t handler_id)
{
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
