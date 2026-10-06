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
#include "zend_exceptions.h"
#include "zend_interfaces.h"
#include "php_true_async.h"
#include "await.h"
#include "coroutine.h"
#include "exceptions.h"
#include "future.h"
#include "scheduler.h"

///////////////////////////////////////////////////////////////////
/// Awaitables and tokens
///////////////////////////////////////////////////////////////////

async_awaitable_t *async_await_awaitable_of(zend_object *object)
{
	async_awaitable_t *awaitable = async_awaitable_from_object(object);

	if (UNEXPECTED(awaitable == NULL)) {
		zend_throw_exception(async_ce_async_exception, "Future has no state", 0);
	}

	return awaitable;
}

void async_awaitable_addref(async_awaitable_t *awaitable)
{
	if (ASYNC_AWAITABLE_IS_COROUTINE(awaitable)) {
		GC_ADDREF(&((async_coroutine_t *) awaitable)->std);
	} else {
		((async_event_t *) awaitable)->ref_count++;
	}
}

void async_awaitable_release(async_awaitable_t *awaitable)
{
	if (ASYNC_AWAITABLE_IS_COROUTINE(awaitable)) {
		OBJ_RELEASE(&((async_coroutine_t *) awaitable)->std);
	} else {
		async_future_event_release((async_future_event_t *) awaitable);
	}
}

static void await_mark_handled(async_awaitable_t *awaitable)
{
	if (ASYNC_AWAITABLE_IS_COROUTINE(awaitable)) {
		((async_coroutine_t *) awaitable)->coroutine.flags |= ASYNC_COROUTINE_F_EXCEPTION_HANDLED;
	} else {
		((async_event_t *) awaitable)->flags |= ASYNC_EVENT_F_EXCEPTION_HANDLED;
	}
}

/* Observed from the call on: the outcome goes to the waiter (async_API.c:1007-1008). */
static void await_mark_observed(async_awaitable_t *awaitable)
{
	if (ASYNC_AWAITABLE_IS_COROUTINE(awaitable)) {
		((async_coroutine_t *) awaitable)->coroutine.flags |= ASYNC_COROUTINE_F_EXC_CAUGHT;
	} else {
		((async_event_t *) awaitable)->flags |= ASYNC_EVENT_F_RESULT_USED | ASYNC_EVENT_F_EXC_CAUGHT;
	}
}

/* The outcome of a completed awaitable, borrowed; false while it runs. `result` may be UNDEF. */
static bool await_outcome(async_awaitable_t *awaitable, zval **result, zend_object **exception)
{
	if (ASYNC_AWAITABLE_IS_COROUTINE(awaitable)) {
		async_coroutine_t *coroutine = (async_coroutine_t *) awaitable;

		*result = &coroutine->coroutine.result;
		*exception = coroutine->coroutine.exception;

		return ZEND_COROUTINE_IS_FINISHED(&coroutine->coroutine);
	}

	async_future_event_t *future = (async_future_event_t *) awaitable;

	*result = &future->result;
	*exception = future->exception;

	return (future->base.flags & ASYNC_EVENT_F_CLOSED) != 0;
}

/* TrueAsync's async_resolve_cancel_token (async_API.c:1253-1281): always OperationCanceledException,
 * so a catch can tell the token's exception from the awaitable's. */
static zend_object *await_cancelled_error(zend_object *previous)
{
	zend_object *error = async_new_exception(async_ce_operation_canceled, "Operation has been cancelled");

	if (previous != NULL) {
		GC_ADDREF(previous);
		zend_exception_set_previous(error, previous);
	}

	return error;
}

bool async_await_token_check(async_awaitable_t *token)
{
	const bool is_coroutine = ASYNC_AWAITABLE_IS_COROUTINE(token);

	if (!is_coroutine) {
		((async_event_t *) token)->flags |= ASYNC_EVENT_F_RESULT_USED | ASYNC_EVENT_F_EXC_CAUGHT;
	}

	zval *result;
	zend_object *exception;

	if (EXPECTED(!await_outcome(token, &result, &exception))) {
		return true;
	}

	/* The exception goes to the waiter as the previous: the token's outcome is observed. */
	if (exception != NULL && is_coroutine) {
		((async_coroutine_t *) token)->coroutine.flags |= ASYNC_COROUTINE_F_EXC_CAUGHT;
	}

	zend_throw_exception_internal(await_cancelled_error(exception));

	return false;
}

/* The token's notify, or the teardown of a token whose notify stopped at a throwing callback before
 * this record: the teardown passes no outcome, which the token holds. */
static void
token_record_wake(async_awaitable_t *target, async_event_callback_t *callback, void *result, zend_object *exception)
{
	(void) result;

	async_coroutine_event_callback_t *record = (async_coroutine_event_callback_t *) callback;

	if (UNEXPECTED(record->event == NULL)) {
		zval *own_result;

		await_outcome(target, &own_result, &exception);
	}

	if (exception != NULL) {
		await_mark_handled(target);
	}

	async_scheduler_enqueue(&record->coroutine->coroutine, await_cancelled_error(exception), true);
}

static zend_string *token_record_info(const async_coroutine_event_callback_t *record)
{
	if (ASYNC_AWAITABLE_IS_COROUTINE(record->event)) {
		return zend_strpprintf(
				0, "cancellation: coroutine #%u", ((const async_coroutine_t *) record->event)->std.handle);
	}

	return zend_string_init(ZEND_STRL("cancellation: future"), 0);
}

static const async_wait_kind_t async_wait_kind_token = {
	.info = token_record_info,
};

void async_await_token_link(async_coroutine_event_callback_t *record,
							async_coroutine_t *waiter,
							async_awaitable_t *token)
{
	async_wait_link(record, waiter, token, &async_wait_kind_token, token_record_wake);
}

///////////////////////////////////////////////////////////////////
/// The await_* context
///////////////////////////////////////////////////////////////////

typedef struct _await_context_s await_context_t;

/* One trigger of an await_* wait that had not completed when the wait reached it. */
typedef struct
{
	async_coroutine_event_callback_t record;
	await_context_t *context;
	/* A reference: a Future object may let go of its event during the wait (a second __construct()),
	 * and a generator's item has no other holder. */
	async_awaitable_t *target;
	zval key;   /* LONG, STRING, or NULL for the next index of the tables */
	bool taken; /* its outcome went into the tables */
} await_trigger_t;

/* Triggers never move once linked: a vector holds their addresses. */
typedef struct _await_chunk_s
{
	struct _await_chunk_s *next;
	uint32_t length;
	uint32_t capacity;
	await_trigger_t triggers[];
} await_chunk_t;

#define AWAIT_ITERATOR_CHUNK 32

/* The block of an await_* wait (S5.md section 5), as TrueAsync's async_await_context_t
 * (async_API.h:27-63). The tables are the caller's: nothing writes into them once `finished`. */
struct _await_context_s
{
	async_wait_block_t head;
	uint32_t ref_count;     /* the waiter, and the iterator coroutine of a Traversable */
	uint32_t waiting_count; /* the outcomes to wait for (successes when errors are collected); 0: all */
	uint32_t total;         /* the triggers that are not null; known at the iterator's end */
	uint32_t resolved_count;
	uint32_t success_count;
	uint32_t seen_count; /* the iterator's triggers so far */
	uint32_t rest_count; /* the coroutines the wait for the rest still waits for */
	bool finished;       /* the wait is unlinked: nothing links or writes any more */
	bool iterating;      /* the iterator coroutine has not ended */
	bool fill_null;
	bool preserve_key_order;
	HashTable *results;
	HashTable *errors;        /* NULL: an error ends the wait */
	async_awaitable_t *token; /* held by await_triggers() for the wait; NULL without */
	async_coroutine_t *waiter;
	await_chunk_t *chunks;          /* the newest first */
	zend_object_iterator *iterator; /* the Traversable's, until the iterator coroutine ends it */
};

static void await_context_release(await_context_t *context)
{
	if (--context->ref_count > 0) {
		return;
	}

	await_chunk_t *chunk = context->chunks;

	while (chunk != NULL) {
		await_chunk_t *next = chunk->next;

		for (uint32_t i = 0; i < chunk->length; i++) {
			ZEND_ASSERT(chunk->triggers[i].record.event == NULL && "a released wait is unlinked");
			zval_ptr_dtor(&chunk->triggers[i].key);
			async_awaitable_release(chunk->triggers[i].target);
		}

		efree(chunk);
		chunk = next;
	}

	if (UNEXPECTED(context->iterator != NULL)) {
		zend_iterator_dtor(context->iterator);
	}

	efree(context);
}

static void await_block_unlink(async_wait_block_t *head)
{
	await_context_t *context = (await_context_t *) head;

	if (context->finished) {
		return;
	}

	context->finished = true;

	for (await_chunk_t *chunk = context->chunks; chunk != NULL; chunk = chunk->next) {
		for (uint32_t i = 0; i < chunk->length; i++) {
			async_wait_record_unlink(&chunk->triggers[i].record);
		}
	}
}

static void await_block_release(async_wait_block_t *head)
{
	await_context_release((await_context_t *) head);
}

static void await_block_walk(async_wait_block_t *head,
							 void (*visit)(const async_coroutine_event_callback_t *record, void *arg),
							 void *arg)
{
	const await_context_t *context = (const await_context_t *) head;

	for (const await_chunk_t *chunk = context->chunks; chunk != NULL; chunk = chunk->next) {
		for (uint32_t i = 0; i < chunk->length; i++) {
			if (chunk->triggers[i].record.event != NULL) {
				visit(&chunk->triggers[i].record, arg);
			}
		}
	}
}

static const async_wait_block_ops_t await_block_ops = {
	.unlink = await_block_unlink,
	.release = await_block_release,
	.walk = await_block_walk,
};

static await_chunk_t *await_chunk_add(await_context_t *context, const uint32_t capacity)
{
	await_chunk_t *chunk = emalloc(sizeof(await_chunk_t) + capacity * sizeof(await_trigger_t));

	chunk->next = context->chunks;
	chunk->length = 0;
	chunk->capacity = capacity;
	context->chunks = chunk;

	return chunk;
}

/* A trigger of `chunk` for `target` under `key`, not linked yet. */
static await_trigger_t *
await_trigger_add(await_context_t *context, await_chunk_t *chunk, async_awaitable_t *target, const zval *key)
{
	ZEND_ASSERT(chunk->length < chunk->capacity);
	await_trigger_t *trigger = &chunk->triggers[chunk->length++];

	trigger->record.event = NULL;
	trigger->context = context;
	trigger->target = target;
	trigger->taken = false;
	async_awaitable_addref(target);
	ZVAL_COPY(&trigger->key, key);

	return trigger;
}

///////////////////////////////////////////////////////////////////
/// Outcomes
///////////////////////////////////////////////////////////////////

/* Stores under `key`, or under the table's next index for a NULL key, which `key` then becomes, as
 * TrueAsync's callback does (async_API.c:398-407). Takes `value`. */
static void await_table_update(HashTable *table, zval *key, zval *value)
{
	if (Z_TYPE_P(key) == IS_STRING) {
		zend_hash_update(table, Z_STR_P(key), value);
		return;
	}

	if (Z_TYPE_P(key) == IS_LONG) {
		zend_hash_index_update(table, Z_LVAL_P(key), value);
		return;
	}

	if (UNEXPECTED(zend_hash_next_index_insert(table, value) == NULL)) {
		zval_ptr_dtor(value);
		return;
	}

	ZVAL_LONG(key, table->nNextFreeElement - 1);
}

/* The places of the results in the input's order: NULL with `fill_null`, else markers that the end
 * of the wait removes (TrueAsync's PTR NULL, async_API.c:980-1027 and 580-596). */
static void await_reserve_result(const await_context_t *context, const zval *key)
{
	zval marker;

	if (context->fill_null) {
		ZVAL_NULL(&marker);
	} else if (context->preserve_key_order) {
		ZVAL_PTR(&marker, NULL);
	} else {
		return;
	}

	if (Z_TYPE_P(key) == IS_STRING) {
		zend_hash_add(context->results, Z_STR_P(key), &marker);
	} else if (Z_TYPE_P(key) == IS_LONG) {
		zend_hash_index_add(context->results, Z_LVAL_P(key), &marker);
	}
}

static bool await_is_satisfied(const await_context_t *context)
{
	const uint32_t done = context->errors != NULL ? context->success_count : context->resolved_count;

	return (context->waiting_count > 0 && done >= context->waiting_count) ||
			(!context->iterating && context->total != 0 && context->resolved_count >= context->total);
}

/* An error a collecting wait keeps under `key`. */
static void await_keep_error(const await_context_t *context, zval *key, zend_object *exception)
{
	zval value;

	ZVAL_OBJ_COPY(&value, exception);
	await_table_update(context->errors, key, &value);
}

/* One trigger's outcome into the tables (TrueAsync's async_waiting_callback, async_API.c:370-459):
 * true when the wait is over, with `*error` (borrowed) when it throws. */
static bool await_take(await_context_t *context, zval *key, zval *result, zend_object *exception, zend_object **error)
{
	context->resolved_count++;

	if (exception != NULL) {
		if (context->errors == NULL) {
			*error = exception;
			return true;
		}

		await_keep_error(context, key, exception);

		return !context->iterating && context->resolved_count >= context->total;
	}

	zval value;

	context->success_count++;

	if (result == NULL || Z_ISUNDEF_P(result)) {
		ZVAL_NULL(&value);
	} else {
		ZVAL_COPY_DEREF(&value, result);
	}

	await_table_update(context->results, key, &value);

	return await_is_satisfied(context);
}

/* Ends the wait: a token that has completed wins over a trigger that ends it in the same notify
 * (S5.md section 5, test await/071). `error` is borrowed. */
static void await_wake_waiter(const await_context_t *context, zend_object *error)
{
	zval *token_result;
	zend_object *token_exception;

	if (context->token != NULL && UNEXPECTED(await_outcome(context->token, &token_result, &token_exception))) {
		async_scheduler_enqueue(&context->waiter->coroutine, await_cancelled_error(token_exception), true);
		return;
	}

	async_scheduler_enqueue(&context->waiter->coroutine, error, false);
}

///////////////////////////////////////////////////////////////////
/// Records
///////////////////////////////////////////////////////////////////

static zend_string *await_record_info(const async_coroutine_event_callback_t *record)
{
	if (ASYNC_AWAITABLE_IS_COROUTINE(record->event)) {
		return zend_strpprintf(0, "await: coroutine #%u", ((const async_coroutine_t *) record->event)->std.handle);
	}

	return zend_string_init(ZEND_STRL("await: future"), 0);
}

static const async_wait_kind_t async_wait_kind_trigger = {
	.info = await_record_info,
};

/* A trigger completed, or its teardown fired the record a throwing callback left; the teardown
 * passes no outcome, which the trigger holds. */
static void
trigger_record_wake(async_awaitable_t *target, async_event_callback_t *callback, void *result, zend_object *exception)
{
	await_trigger_t *trigger = (await_trigger_t *) callback;
	await_context_t *context = trigger->context;

	if (UNEXPECTED(trigger->record.event == NULL)) {
		zval *own_result;

		await_outcome(trigger->target, &own_result, &exception);
		result = own_result;
	} else {
		async_wait_record_unlink(&trigger->record);
	}

	if (exception != NULL) {
		await_mark_handled(target);
	}

	zend_object *error = NULL;

	trigger->taken = true;

	if (await_take(context, &trigger->key, result, exception, &error)) {
		await_wake_waiter(context, error);
	}
}

/* The wait for the rest (TrueAsync's async_waiting_cancellation_callback, async_API.c:471-516): the
 * errors join the table, and the waiter wakes with the last coroutine. */
static void
rest_record_wake(async_awaitable_t *target, async_event_callback_t *callback, void *result, zend_object *exception)
{
	(void) result;

	await_trigger_t *trigger = (await_trigger_t *) callback;
	await_context_t *context = trigger->context;

	async_wait_record_unlink(&trigger->record);
	exception = ((async_coroutine_t *) target)->coroutine.exception;

	if (exception != NULL) {
		await_mark_handled(target);

		if (context->errors != NULL) {
			await_keep_error(context, &trigger->key, exception);
		}
	}

	if (--context->rest_count == 0) {
		async_scheduler_enqueue(&context->waiter->coroutine, NULL, false);
	}
}

static zend_string *iterator_record_info(const async_coroutine_event_callback_t *record)
{
	return zend_strpprintf(0, "await: iterator coroutine #%u", ((const async_coroutine_t *) record->event)->std.handle);
}

static const async_wait_kind_t async_wait_kind_iterator = {
	.info = iterator_record_info,
};

/* The iterator coroutine ended (TrueAsync's iterator_coroutine_finish_callback and
 * await_iterator_dispose, async_API.c:754-772, 640-673): the triggers it saw are all there are. */
static void
iterator_record_wake(async_awaitable_t *target, async_event_callback_t *callback, void *result, zend_object *exception)
{
	(void) result;
	(void) exception;

	async_coroutine_event_callback_t *record = (async_coroutine_event_callback_t *) callback;
	async_coroutine_t *iterator_coroutine = (async_coroutine_t *) target;
	await_context_t *context = iterator_coroutine->coroutine.extended_data;
	zend_object *iterator_exception = iterator_coroutine->coroutine.exception;

	if (UNEXPECTED(iterator_exception != NULL)) {
		iterator_coroutine->coroutine.flags |= ASYNC_COROUTINE_F_EXCEPTION_HANDLED;
		async_scheduler_enqueue(&record->coroutine->coroutine, iterator_exception, false);
		return;
	}

	context->iterating = false;
	context->total = context->seen_count;

	if (context->total == 0 || await_is_satisfied(context)) {
		await_wake_waiter(context, NULL);
		return;
	}

	async_wait_record_unlink(record);
}

///////////////////////////////////////////////////////////////////
/// Triggers
///////////////////////////////////////////////////////////////////

/* The awaitable of an item, or NULL: skipped for a null one, else with an exception thrown. */
static async_awaitable_t *await_trigger_of(zval *item, const async_coroutine_t *waiter, bool *skip)
{
	ZVAL_DEREF(item);
	*skip = false;

	if (Z_TYPE_P(item) == IS_NULL || Z_TYPE_P(item) == IS_UNDEF) {
		*skip = true;
		return NULL;
	}

	if (UNEXPECTED(Z_TYPE_P(item) != IS_OBJECT ||
				   (Z_OBJCE_P(item) != async_ce_coroutine && Z_OBJCE_P(item) != async_ce_future))) {
		zend_throw_exception(async_ce_async_exception, "Expected item to be an Async\\Awaitable object", 0);
		return NULL;
	}

	async_awaitable_t *awaitable = async_await_awaitable_of(Z_OBJ_P(item));

	if (UNEXPECTED(awaitable == (const async_awaitable_t *) waiter)) {
		zend_throw_error(NULL, "Cannot await a coroutine from within itself");
		return NULL;
	}

	return awaitable;
}

/* Reserves one more slot in `awaitable`'s vector for this wait: `counts` holds how many records of
 * the wait go there, so a trigger that repeats gets a slot per record (S3.md 4.1, phase 2). */
static void await_reserve(HashTable *counts, async_awaitable_t *awaitable)
{
	const zend_ulong address = (zend_ulong) (uintptr_t) awaitable;
	zval *count = zend_hash_index_find(counts, address);
	zval one;

	if (count == NULL) {
		ZVAL_LONG(&one, 1);
		count = zend_hash_index_add_new(counts, address, &one);
	} else {
		Z_LVAL_P(count)++;
	}

	async_callbacks_reserve(async_awaitable_callbacks(awaitable), (uint32_t) Z_LVAL_P(count));
}

static void await_key_of(zval *key, const zend_ulong index, zend_string *string_key)
{
	if (string_key != NULL) {
		ZVAL_STR(key, string_key);
	} else {
		ZVAL_LONG(key, index);
	}
}

/* An item of the array that is a coroutine still running, or NULL. */
static async_awaitable_t *await_running_coroutine(zval *item)
{
	ZVAL_DEREF(item);

	if (Z_TYPE_P(item) != IS_OBJECT || Z_OBJCE_P(item) != async_ce_coroutine) {
		return NULL;
	}

	async_coroutine_t *coroutine = async_coroutine_from_object(Z_OBJ_P(item));

	return ZEND_COROUTINE_IS_FINISHED(&coroutine->coroutine) ? NULL : (async_awaitable_t *) coroutine;
}

static bool await_is_untaken_coroutine(const await_trigger_t *trigger)
{
	return !trigger->taken && ASYNC_AWAITABLE_IS_COROUTINE(trigger->target);
}

/* A coroutine the parked wait did not take that finished since the wake: its record left with the
 * wait's, so its error joins the table here. */
static void await_take_late_error(const await_context_t *context, await_trigger_t *trigger)
{
	zend_object *exception = ((async_coroutine_t *) trigger->target)->coroutine.exception;

	trigger->taken = true;

	if (exception != NULL) {
		await_mark_handled(trigger->target);

		if (context->errors != NULL) {
			await_keep_error(context, &trigger->key, exception);
		}
	}
}

/* Parks the waiter on its block until the wait ends; the block goes back to the caller's reference. */
static bool await_park(async_coroutine_t *waiter)
{
	const bool woken = ZEND_ASYNC_SUSPEND();
	async_wait_block_t *block = async_wait_take_block(waiter);

	ZEND_ASSERT(block != NULL && "the waiter takes back its own block");
	(void) block;

	return woken;
}

/* After the wait, until every coroutine of the array still running has finished (TrueAsync's
 * cancel_on_exit, async_cancel_awaited_futures, async_API.c:800-873): it cancels nothing, despite
 * the name, and the token does not end it. A wait that parked goes over the triggers it did not
 * take; one that ended in place goes over the array, marking what the pass did not reach. */
static void await_rest(await_context_t *context, HashTable *items)
{
	async_coroutine_t *waiter = context->waiter;
	await_chunk_t *waited = context->chunks;
	zend_ulong index;
	zend_string *string_key;
	zval *item;
	uint32_t count = 0;

	if (waited == NULL) {
		ZEND_HASH_FOREACH_VAL(items, item)
		{
			count += await_running_coroutine(item) != NULL;
		}
		ZEND_HASH_FOREACH_END();
	} else {
		for (await_chunk_t *chunk = waited; chunk != NULL; chunk = chunk->next) {
			for (uint32_t i = 0; i < chunk->length; i++) {
				await_trigger_t *trigger = &chunk->triggers[i];

				if (!await_is_untaken_coroutine(trigger)) {
					continue;
				}

				if (ZEND_COROUTINE_IS_FINISHED(&((async_coroutine_t *) trigger->target)->coroutine)) {
					await_take_late_error(context, trigger);
				} else {
					count++;
				}
			}
		}
	}

	if (count == 0) {
		return;
	}

	await_chunk_t *rest = await_chunk_add(context, count);
	HashTable counts;

	context->finished = false;
	context->rest_count = count;
	waiter->waker.block = &context->head;
	context->ref_count++;

	zend_hash_init(&counts, count, NULL, NULL, false);

	if (waited == NULL) {
		ZEND_HASH_FOREACH_KEY_VAL(items, index, string_key, item)
		{
			async_awaitable_t *coroutine = await_running_coroutine(item);

			if (coroutine == NULL) {
				continue;
			}

			zval key;

			await_key_of(&key, index, string_key);
			await_mark_observed(coroutine);
			await_trigger_add(context, rest, coroutine, &key);
			await_reserve(&counts, coroutine);
		}
		ZEND_HASH_FOREACH_END();
	} else {
		for (await_chunk_t *chunk = waited; chunk != NULL; chunk = chunk->next) {
			for (uint32_t i = 0; i < chunk->length; i++) {
				const await_trigger_t *trigger = &chunk->triggers[i];

				if (await_is_untaken_coroutine(trigger)) {
					await_trigger_add(context, rest, trigger->target, &trigger->key);
					await_reserve(&counts, trigger->target);
				}
			}
		}
	}

	zend_hash_destroy(&counts);

	for (uint32_t i = 0; i < rest->length; i++) {
		async_wait_link(&rest->triggers[i].record,
						waiter,
						rest->triggers[i].target,
						&async_wait_kind_trigger,
						rest_record_wake);
	}

	await_park(waiter);
	await_context_release(context);
}

/* The array pass (S5.md section 5): every item checked before any side effect, then marked and
 * taken in place when it has completed, up to the one that satisfies the wait; the rest linked,
 * unless the wait ended (TrueAsync's fix #103). */
static void await_array(await_context_t *context, HashTable *items, const bool wait_for_rest)
{
	async_coroutine_t *waiter = context->waiter;
	zend_ulong index;
	zend_string *string_key;
	zval *item;
	bool skip;

	ZEND_HASH_FOREACH_VAL(items, item)
	{
		if (UNEXPECTED(await_trigger_of(item, waiter, &skip) == NULL && !skip)) {
			return;
		}

		context->total += !skip;
	}
	ZEND_HASH_FOREACH_END();

	if (context->total == 0) {
		return;
	}

	if (context->waiting_count == 0) {
		context->waiting_count = context->total;
	}

	uint32_t pending_count = 0;
	bool done = false;
	zend_object *error = NULL;

	ZEND_HASH_FOREACH_KEY_VAL(items, index, string_key, item)
	{
		async_awaitable_t *awaitable = await_trigger_of(item, waiter, &skip);

		if (skip) {
			continue;
		}

		zval key;
		zval *result;
		zend_object *exception;

		await_key_of(&key, index, string_key);
		await_mark_observed(awaitable);

		/* The wait throws, and TrueAsync's pass goes on (async_API.c:1043-1045): every later error
		 * gets the one before as its previous and is thrown instead, as its waker does
		 * (coroutine.c:810-816). */
		if (UNEXPECTED(error != NULL)) {
			if (await_outcome(awaitable, &result, &exception) && exception != NULL && exception != error) {
				GC_ADDREF(error);
				zend_exception_set_previous(exception, error);
				error = exception;
			}

			continue;
		}

		await_reserve_result(context, &key);

		if (!await_outcome(awaitable, &result, &exception)) {
			pending_count++;
			continue;
		}

		done = await_take(context, &key, result, exception, &error);

		if (done && error == NULL) {
			break;
		}
	}
	ZEND_HASH_FOREACH_END();

	if (done) {
		if (UNEXPECTED(error != NULL)) {
			GC_ADDREF(error);
			zend_throw_exception_internal(error);
			return;
		}

		if (wait_for_rest) {
			await_rest(context, items);
		}

		return;
	}

	ZEND_ASSERT(pending_count != 0 && "every trigger completed means the wait is over");

	/* The block goes into the waker before the reservations: a bailout out of one leaves it there,
	 * and the finish releases the waker's reference. */
	await_chunk_t *chunk = await_chunk_add(context, pending_count);
	HashTable counts;

	waiter->waker.block = &context->head;
	context->ref_count++;

	zend_hash_init(&counts, pending_count + 1, NULL, NULL, false);

	ZEND_HASH_FOREACH_KEY_VAL(items, index, string_key, item)
	{
		async_awaitable_t *awaitable = await_trigger_of(item, waiter, &skip);
		zval *result;
		zend_object *exception;

		if (skip || await_outcome(awaitable, &result, &exception)) {
			continue;
		}

		zval key;

		await_key_of(&key, index, string_key);
		await_trigger_add(context, chunk, awaitable, &key);
		await_reserve(&counts, awaitable);
	}
	ZEND_HASH_FOREACH_END();

	if (context->token != NULL) {
		await_reserve(&counts, context->token);
	}

	zend_hash_destroy(&counts);

	for (uint32_t i = 0; i < chunk->length; i++) {
		async_wait_link(&chunk->triggers[i].record,
						waiter,
						chunk->triggers[i].target,
						&async_wait_kind_trigger,
						trigger_record_wake);
	}

	if (context->token != NULL) {
		async_await_token_link(&waiter->waker.records[0], waiter, context->token);
	}

	const bool woken = await_park(waiter);
	await_context_release(context);

	if (EXPECTED(woken) && wait_for_rest) {
		await_rest(context, items);
	}
}

///////////////////////////////////////////////////////////////////
/// Traversables
///////////////////////////////////////////////////////////////////

/* One item of the Traversable: taken in place when it has completed, else linked into the parked
 * waiter's block. False with an exception, which ends the iteration and the wait. */
static bool await_iterator_item(await_context_t *context, zval *item, zval *key)
{
	if (UNEXPECTED(context->finished)) {
		return true;
	}

	bool skip;
	async_awaitable_t *awaitable = await_trigger_of(item, context->waiter, &skip);

	if (skip) {
		return true;
	}

	if (UNEXPECTED(awaitable == NULL)) {
		return false;
	}

	if (UNEXPECTED(Z_TYPE_P(key) != IS_STRING && Z_TYPE_P(key) != IS_LONG && Z_TYPE_P(key) != IS_NULL)) {
		zend_throw_exception(async_ce_async_exception, "Invalid key type: must be string, long or null", 0);
		return false;
	}

	context->seen_count++;
	await_mark_observed(awaitable);
	await_reserve_result(context, key);

	zval *result;
	zend_object *exception;

	if (await_outcome(awaitable, &result, &exception)) {
		zend_object *error = NULL;

		if (await_take(context, key, result, exception, &error)) {
			await_wake_waiter(context, error);
		}

		return true;
	}

	await_chunk_t *chunk = context->chunks;

	if (chunk == NULL || chunk->length == chunk->capacity) {
		chunk = await_chunk_add(context, AWAIT_ITERATOR_CHUNK);
	}

	await_trigger_t *trigger = await_trigger_add(context, chunk, awaitable, key);

	async_callbacks_reserve(async_awaitable_callbacks(awaitable), 1);
	async_wait_link(&trigger->record, context->waiter, awaitable, &async_wait_kind_trigger, trigger_record_wake);

	return true;
}

/* The iterator coroutine (S5.md section 5): it walks the Traversable while the waiter is parked, so
 * a Traversable whose own code suspends does not hold back the triggers already linked. Whatever
 * it throws ends its body, and its end the wait. Once the wait is over it goes on walking without
 * linking or writing, as TrueAsync's iterator does (test await/049), until the waiter's exit
 * cancels it. */
static void await_iterator_entry(void)
{
	const async_coroutine_t *coroutine = (const async_coroutine_t *) ZEND_ASYNC_CURRENT_COROUTINE;
	await_context_t *context = coroutine->coroutine.extended_data;
	zend_object_iterator *iterator = context->iterator;

	iterator->index = 0;

	if (iterator->funcs->rewind != NULL) {
		iterator->funcs->rewind(iterator);
	}

	while (EXPECTED(EG(exception) == NULL)) {
		if (iterator->funcs->valid(iterator) != SUCCESS || UNEXPECTED(EG(exception) != NULL)) {
			break;
		}

		zval *item = iterator->funcs->get_current_data(iterator);

		if (UNEXPECTED(EG(exception) != NULL)) {
			break;
		}

		zval key;

		if (iterator->funcs->get_current_key != NULL) {
			iterator->funcs->get_current_key(iterator, &key);

			if (UNEXPECTED(EG(exception) != NULL)) {
				zval_ptr_dtor(&key);
				break;
			}
		} else {
			ZVAL_LONG(&key, iterator->index);
		}

		if (item != NULL && UNEXPECTED(!await_iterator_item(context, item, &key))) {
			zval_ptr_dtor(&key);
			break;
		}

		zval_ptr_dtor(&key);
		iterator->index++;
		iterator->funcs->move_forward(iterator);
	}

	context->iterator = NULL;
	zend_iterator_dtor(iterator);
}

/* The iterator coroutine's reference, for one that never ran too. */
static void await_iterator_dispose(zend_coroutine_t *coroutine)
{
	await_context_t *context = coroutine->extended_data;

	coroutine->extended_data = NULL;
	await_context_release(context);
}

/* Takes `iterator`. */
static void await_traversable(await_context_t *context, zend_object_iterator *iterator)
{
	async_coroutine_t *waiter = context->waiter;
	async_coroutine_t *iterator_coroutine = async_coroutine_new();

	context->iterator = iterator;
	context->iterating = true;
	context->ref_count++;
	iterator_coroutine->coroutine.internal_entry = await_iterator_entry;
	iterator_coroutine->coroutine.extended_data = context;
	iterator_coroutine->coroutine.extended_dispose = await_iterator_dispose;
	/* Its exception goes to the waiter, or nowhere once the waiter has left. */
	iterator_coroutine->coroutine.flags |= ASYNC_COROUTINE_F_EXC_CAUGHT;

	if (UNEXPECTED(!async_scheduler_enqueue(&iterator_coroutine->coroutine, NULL, false))) {
		zend_hash_index_del(&ASYNC_G(coroutines), iterator_coroutine->std.handle);
		OBJ_RELEASE(&iterator_coroutine->std);
		return;
	}

	GC_ADDREF(&iterator_coroutine->std);

	waiter->waker.block = &context->head;
	context->ref_count++;

	async_callbacks_reserve(&iterator_coroutine->callbacks, 1);

	if (context->token != NULL) {
		async_callbacks_reserve(async_awaitable_callbacks(context->token), 1);
		async_await_token_link(&waiter->waker.records[0], waiter, context->token);
	}

	async_wait_link(&waiter->waker.records[1],
					waiter,
					(async_awaitable_t *) iterator_coroutine,
					&async_wait_kind_iterator,
					iterator_record_wake);

	await_park(waiter);
	await_context_release(context);

	/* A Traversable that suspends may still be running: it stops at its next step either way, and the
	 * cancellation stops it inside the step. */
	if (!ZEND_COROUTINE_IS_FINISHED(&iterator_coroutine->coroutine)) {
		async_coroutine_cancel(iterator_coroutine, NULL, false);
	}

	OBJ_RELEASE(&iterator_coroutine->std);
}

///////////////////////////////////////////////////////////////////
/// The family
///////////////////////////////////////////////////////////////////

typedef struct
{
	zend_long count; /* 0 or less: all */
	bool collect_errors;
	bool fill_null;
	bool preserve_key_order;
	bool wait_for_rest;
} await_options_t;

/* The wait with the token held. The token is checked where no PHP code runs before the first link:
 * after the Traversable's getIterator(), which may complete it. */
static void await_wait(async_coroutine_t *waiter,
					   zval *items,
					   async_awaitable_t *token,
					   const await_options_t *options,
					   HashTable *results,
					   HashTable *errors)
{
	/* A bailout that a shutdown function's zend_try caught can leave main's wait linked. */
	async_wait_end(waiter);

	HashTable *array = Z_TYPE_P(items) == IS_ARRAY ? Z_ARRVAL_P(items) : NULL;
	zend_object_iterator *iterator = NULL;

	if (array == NULL) {
		zend_class_entry *class_entry = Z_OBJCE_P(items);

		iterator = class_entry->get_iterator(class_entry, items, 0);

		if (UNEXPECTED(iterator == NULL)) {
			if (EG(exception) == NULL) {
				zend_throw_exception(async_ce_async_exception, "Failed to create iterator", 0);
			}

			return;
		}
	}

	if (token != NULL && UNEXPECTED(!async_await_token_check(token))) {
		if (iterator != NULL) {
			zend_iterator_dtor(iterator);
		}

		return;
	}

	HashTable *ordered = NULL;

	if (options->preserve_key_order && !options->fill_null) {
		ordered = zend_new_array(array != NULL ? zend_hash_num_elements(array) : 8);
	}

	await_context_t *context = ecalloc(1, sizeof(await_context_t));

	context->head.ops = &await_block_ops;
	context->ref_count = 1;
	context->waiting_count = options->count > 0 ? (uint32_t) MIN(options->count, UINT32_MAX) : 0;
	context->fill_null = options->fill_null;
	context->preserve_key_order = options->preserve_key_order;
	context->results = ordered != NULL ? ordered : results;
	context->errors = errors;
	context->token = token;
	context->waiter = waiter;

	if (array != NULL) {
		await_array(context, array, options->wait_for_rest);
	} else {
		/* The Traversable's own options: TrueAsync waits for the rest only of an array (async_API.c:808-811). */
		await_traversable(context, iterator);
	}

	/* The iterator coroutine may hold the context past the wait, and the token's reference is the
	 * caller's. */
	context->token = NULL;
	await_context_release(context);

	if (ordered == NULL) {
		return;
	}

	if (EXPECTED(EG(exception) == NULL)) {
		zend_ulong index;
		zend_string *string_key;
		zval *value;

		ZEND_HASH_FOREACH_KEY_VAL(ordered, index, string_key, value)
		{
			if (Z_TYPE_P(value) == IS_PTR) {
				continue;
			}

			Z_TRY_ADDREF_P(value);

			if (string_key != NULL) {
				zend_hash_update(results, string_key, value);
			} else {
				zend_hash_index_update(results, index, value);
			}
		}
		ZEND_HASH_FOREACH_END();
	}

	zend_array_release(ordered);
}

/* TrueAsync's async_await_futures (async_API.c:892-1171) over an array or a Traversable: `results`
 * and `errors` (NULL unless errors are collected) are the caller's. */
static void await_triggers(
		zval *items, zend_object *cancellation, const await_options_t *options, HashTable *results, HashTable *errors)
{
	if (UNEXPECTED(Z_TYPE_P(items) != IS_ARRAY &&
				   (Z_TYPE_P(items) != IS_OBJECT || Z_OBJCE_P(items)->get_iterator == NULL))) {
		zend_throw_exception(async_ce_async_exception,
							 "Expected parameter 'iterable' to be an array or an object implementing Traversable",
							 0);
		return;
	}

	if (Z_TYPE_P(items) == IS_ARRAY && zend_hash_num_elements(Z_ARRVAL_P(items)) == 0) {
		return;
	}

	async_coroutine_t *waiter = (async_coroutine_t *) ZEND_ASYNC_CURRENT_COROUTINE;

	if (UNEXPECTED(waiter == NULL || ZEND_COROUTINE_IS_FINISHED(&waiter->coroutine))) {
		zend_throw_exception(async_ce_async_exception, "Cannot await futures outside of a coroutine", 0);
		return;
	}

	if (cancellation == NULL) {
		await_wait(waiter, items, NULL, options, results, errors);
		return;
	}

	async_awaitable_t *token = async_await_awaitable_of(cancellation);

	if (UNEXPECTED(token == NULL)) {
		return;
	}

	/* getIterator() may let the token's Future object go of its event (a second __construct()). */
	async_awaitable_addref(token);
	await_wait(waiter, items, token, options, results, errors);
	async_awaitable_release(token);
}

/* `[results, errors]`; takes both tables. */
static void await_return_pair(zval *return_value, zval *results, HashTable *errors)
{
	zval value;

	array_init_size(return_value, 2);
	zend_hash_next_index_insert_new(Z_ARRVAL_P(return_value), results);
	ZVAL_ARR(&value, errors);
	zend_hash_next_index_insert_new(Z_ARRVAL_P(return_value), &value);
}

/* The first value of `results`, or null; releases the table. */
static void await_return_first(zval *return_value, HashTable *results)
{
	zval *first = zend_hash_get_current_data(results);

	if (first != NULL) {
		ZVAL_COPY(return_value, first);
	} else {
		ZVAL_NULL(return_value);
	}

	zend_array_release(results);
}

ZEND_FUNCTION(Async_await_any_or_fail)
{
	zval *items;
	zend_object *cancellation = NULL;

	THROW_IF_UNAVAILABLE();

	ZEND_PARSE_PARAMETERS_START(1, 2)
		Z_PARAM_ZVAL(items)
		Z_PARAM_OPTIONAL
		Z_PARAM_OBJ_OF_CLASS_OR_NULL(cancellation, async_ce_awaitable)
	ZEND_PARSE_PARAMETERS_END();

	const await_options_t options = { .count = 1 };
	HashTable *results = zend_new_array(1);

	await_triggers(items, cancellation, &options, results, NULL);

	if (UNEXPECTED(EG(exception) != NULL)) {
		zend_array_release(results);
		RETURN_THROWS();
	}

	await_return_first(return_value, results);
}

ZEND_FUNCTION(Async_await_first_success)
{
	zval *items;
	zend_object *cancellation = NULL;

	THROW_IF_UNAVAILABLE();

	ZEND_PARSE_PARAMETERS_START(1, 2)
		Z_PARAM_ZVAL(items)
		Z_PARAM_OPTIONAL
		Z_PARAM_OBJ_OF_CLASS_OR_NULL(cancellation, async_ce_awaitable)
	ZEND_PARSE_PARAMETERS_END();

	const await_options_t options = { .count = 1, .collect_errors = true, .wait_for_rest = true };
	HashTable *results = zend_new_array(1);
	HashTable *errors = zend_new_array(8);

	await_triggers(items, cancellation, &options, results, errors);

	if (UNEXPECTED(EG(exception) != NULL)) {
		zend_array_release(results);
		zend_array_release(errors);
		RETURN_THROWS();
	}

	zval first;

	await_return_first(&first, results);
	await_return_pair(return_value, &first, errors);
}

ZEND_FUNCTION(Async_await_all_or_fail)
{
	zval *items;
	zend_object *cancellation = NULL;
	bool preserve_key_order = true;

	THROW_IF_UNAVAILABLE();

	ZEND_PARSE_PARAMETERS_START(1, 3)
		Z_PARAM_ZVAL(items)
		Z_PARAM_OPTIONAL
		Z_PARAM_OBJ_OF_CLASS_OR_NULL(cancellation, async_ce_awaitable)
		Z_PARAM_BOOL(preserve_key_order)
	ZEND_PARSE_PARAMETERS_END();

	/* Always filled with null: the order of the keys is the input's (async.c:507-509). */
	const await_options_t options = { .fill_null = true,
									  .preserve_key_order = preserve_key_order,
									  .wait_for_rest = true };
	HashTable *results = zend_new_array(8);

	await_triggers(items, cancellation, &options, results, NULL);

	if (UNEXPECTED(EG(exception) != NULL)) {
		zend_array_release(results);
		RETURN_THROWS();
	}

	RETURN_ARR(results);
}

ZEND_FUNCTION(Async_await_all)
{
	zval *items;
	zend_object *cancellation = NULL;
	bool preserve_key_order = true;
	bool fill_null = false;

	THROW_IF_UNAVAILABLE();

	ZEND_PARSE_PARAMETERS_START(1, 4)
		Z_PARAM_ZVAL(items)
		Z_PARAM_OPTIONAL
		Z_PARAM_OBJ_OF_CLASS_OR_NULL(cancellation, async_ce_awaitable)
		Z_PARAM_BOOL(preserve_key_order)
		Z_PARAM_BOOL(fill_null)
	ZEND_PARSE_PARAMETERS_END();

	const await_options_t options = {
		.collect_errors = true, .fill_null = fill_null, .preserve_key_order = preserve_key_order, .wait_for_rest = true
	};
	HashTable *results = zend_new_array(8);
	HashTable *errors = zend_new_array(8);

	await_triggers(items, cancellation, &options, results, errors);

	if (UNEXPECTED(EG(exception) != NULL)) {
		zend_array_release(results);
		zend_array_release(errors);
		RETURN_THROWS();
	}

	zval results_value;

	ZVAL_ARR(&results_value, results);
	await_return_pair(return_value, &results_value, errors);
}

ZEND_FUNCTION(Async_await_any_of_or_fail)
{
	zend_long count = 0;
	zval *items;
	zend_object *cancellation = NULL;
	bool preserve_key_order = true;

	THROW_IF_UNAVAILABLE();

	ZEND_PARSE_PARAMETERS_START(2, 4)
		Z_PARAM_LONG(count)
		Z_PARAM_ITERABLE(items)
		Z_PARAM_OPTIONAL
		Z_PARAM_OBJ_OF_CLASS_OR_NULL(cancellation, async_ce_awaitable)
		Z_PARAM_BOOL(preserve_key_order)
	ZEND_PARSE_PARAMETERS_END();

	if (count == 0) {
		RETURN_EMPTY_ARRAY();
	}

	const await_options_t options = { .count = count, .preserve_key_order = preserve_key_order };
	HashTable *results = zend_new_array(8);

	await_triggers(items, cancellation, &options, results, NULL);

	if (UNEXPECTED(EG(exception) != NULL)) {
		zend_array_release(results);
		RETURN_THROWS();
	}

	RETURN_ARR(results);
}

ZEND_FUNCTION(Async_await_any_of)
{
	zend_long count = 0;
	zval *items;
	zend_object *cancellation = NULL;
	bool preserve_key_order = true;
	bool fill_null = false;

	THROW_IF_UNAVAILABLE();

	ZEND_PARSE_PARAMETERS_START(2, 5)
		Z_PARAM_LONG(count)
		Z_PARAM_ZVAL(items)
		Z_PARAM_OPTIONAL
		Z_PARAM_OBJ_OF_CLASS_OR_NULL(cancellation, async_ce_awaitable)
		Z_PARAM_BOOL(preserve_key_order)
		Z_PARAM_BOOL(fill_null)
	ZEND_PARSE_PARAMETERS_END();

	const await_options_t options = { .count = count,
									  .collect_errors = true,
									  .fill_null = fill_null,
									  .preserve_key_order = preserve_key_order,
									  .wait_for_rest = true };
	HashTable *results = zend_new_array(8);
	HashTable *errors = zend_new_array(8);

	await_triggers(items, cancellation, &options, results, errors);

	if (UNEXPECTED(EG(exception) != NULL)) {
		zend_array_release(results);
		zend_array_release(errors);
		RETURN_THROWS();
	}

	zval results_value;

	ZVAL_ARR(&results_value, results);
	await_return_pair(return_value, &results_value, errors);
}
