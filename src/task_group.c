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
#include "zend_closures.h"
#include "zend_exceptions.h"
#include "zend_interfaces.h"
#include "php_true_async.h"
#include "task_group.h"
#include "collector.h"
#include "coroutine.h"
#include "exceptions.h"
#include "future.h"
#include "scheduler.h"
#include "scope.h"
#include "task_group_arginfo.h"
#include "task_set_arginfo.h"

/* Ports TrueAsync's task group (task_group.c of ext/async, cited below by line) onto this extension's scopes,
 * Futures and callbacks, with the departures of section 8 of dev/plans/S9-taskgroup.md. A task is a coroutine
 * spawned into the group's scope with a subscriber in its callbacks: the subscriber's callback records the
 * outcome inside the coroutine's notify, in scheduler context, and its dispose does the rest after the notify
 * (section 2). Dropping the group closes it without waiting: the destructor takes the object again, cancels
 * what still runs, and the closing ends after the finally handlers, where the errors nobody saw are reported
 * (section 5). */

zend_class_entry *async_ce_task_group = NULL;
zend_class_entry *async_ce_task_set = NULL;

static zend_object_handlers task_group_handlers;

/* The group's bits of its event flags word. */
#define TASK_GROUP_F_CONSTRUCTED (1u << ASYNC_EVENT_F_TYPE_SHIFT)
#define TASK_GROUP_F_TASK_SET (1u << (ASYNC_EVENT_F_TYPE_SHIFT + 1))
/* close(), cancel(), dispose() or the destructor: spawn() is refused. */
#define TASK_GROUP_F_SEALED (1u << (ASYNC_EVENT_F_TYPE_SHIFT + 2))
/* Sealed with nothing running or queued; the finally handlers have started. */
#define TASK_GROUP_F_COMPLETED (1u << (ASYNC_EVENT_F_TYPE_SHIFT + 3))
#define TASK_GROUP_F_CANCELLED (1u << (ASYNC_EVENT_F_TYPE_SHIFT + 4))
/* The destructor ran: the closing holds a reference to the object until it ends. */
#define TASK_GROUP_F_CLOSING (1u << (ASYNC_EVENT_F_TYPE_SHIFT + 5))
#define TASK_GROUP_F_CLOSING_ENDED (1u << (ASYNC_EVENT_F_TYPE_SHIFT + 6))
/* The finally run the completion started has not ended. */
#define TASK_GROUP_F_FINALLY_RUNNING (1u << (ASYNC_EVENT_F_TYPE_SHIFT + 7))

#define TASK_GROUP_FUTURES_FIRST_CAPACITY 4

static const char task_group_closed_message[] = "Cannot spawn tasks on a closed TaskGroup";
static const char task_group_empty_race_message[] = "Cannot race on an empty TaskGroup";
static const char task_group_empty_any_message[] = "Cannot call any() on an empty TaskGroup";

typedef enum
{
	TASK_QUEUED,
	TASK_RUNNING,
	TASK_SUCCEEDED,
	TASK_FAILED,
} task_state_t;

typedef struct _task_subscriber_s task_subscriber_t;

/* A task (task_group.c:25-42). The fields of the states it has left are empty. */
struct _async_task_group_entry_s
{
	/* In the group's queued list while QUEUED, in its settled list once it ended. */
	async_task_group_entry_t *previous;
	async_task_group_entry_t *next;
	zval key;
	task_state_t state;
	/* Its error needs no report; set before the end, it covers the failure to come (section 3). */
	bool is_handled;
	/* QUEUED: the callable with its own references and arguments; the cache owns a __call trampoline. */
	zend_fcall_info fci;
	zend_fcall_info_cache fcc;
	/* RUNNING: one reference to the coroutine and its subscriber in the coroutine's callbacks. */
	async_coroutine_t *coroutine;
	task_subscriber_t *subscriber;
	zval result;            /* SUCCEEDED */
	zend_object *exception; /* FAILED, one reference */
};

/* A task's end in its coroutine's callbacks. */
struct _task_subscriber_s
{
	async_event_callback_t callback;
	async_task_group_t *group; /* NULL once the group is freed */
	async_task_group_entry_t *entry;
};

typedef enum
{
	READ_ALL,  /* all(), joinAll() */
	READ_RACE, /* race(), joinNext() */
	READ_ANY,  /* any(), joinAny() */
} read_kind_t;

/* A pending read's Future, recorded in the group's `futures` and in the Future event's callbacks, whose dispose
 * takes it out when the Future settles or goes first: the channel's recvAsync() pattern in place of TrueAsync's
 * dispose override (task_group.c:112-120). */
struct _async_task_group_future_waiter_s
{
	async_event_callback_t on_future;
	async_task_group_t *group;    /* NULL once out of `futures` */
	async_future_event_t *future; /* borrowed: the event disposes `on_future` before it goes */
	read_kind_t kind;
	bool ignore_errors;
};

/* The closing's end, armed where the finally run ends inside a notify (section 5). */
typedef struct
{
	async_event_callback_t callback;
	async_task_group_t *group; /* the closing holds it */
} closing_subscriber_t;

/* The call of a queued task that a cancel ended unstarted, released after the cancel's answers. */
typedef struct
{
	zend_fcall_info fci;
	zend_fcall_info_cache fcc;
} task_call_t;

static zend_always_inline async_task_group_t *task_group_from_object(zend_object *object)
{
	return (async_task_group_t *) ((char *) object - offsetof(async_task_group_t, std));
}

#define THIS_GROUP task_group_from_object(Z_OBJ_P(ZEND_THIS))

static zend_always_inline bool task_group_is_task_set(const async_task_group_t *group)
{
	return (group->base.flags & TASK_GROUP_F_TASK_SET) != 0;
}

/* Nothing runs and nothing is queued (isFinished()); an open group can take tasks again. */
static zend_always_inline bool task_group_is_settled(const async_task_group_t *group)
{
	return group->active_count == 0 && group->queued_count == 0;
}

static zend_always_inline bool task_group_has_slot(const async_task_group_t *group)
{
	return group->concurrency == 0 || group->active_count < group->concurrency;
}

/* The scope the tasks run in: the own scope, or the external Scope object's while it has one; NULL once gone. */
static async_scope_t *task_group_scope(const async_task_group_t *group)
{
	if (group->scope_object != NULL) {
		return async_scope_object_from_object(group->scope_object)->scope;
	}

	return group->scope;
}

static void task_group_complete(async_task_group_t *group);
static void task_group_closing_end(async_task_group_t *group, bool caller_release_only);

///////////////////////////////////////////////////////////////////
/// Entries
///////////////////////////////////////////////////////////////////

static void task_group_entry_list_append(async_task_group_entry_t **head,
										 async_task_group_entry_t **tail,
										 async_task_group_entry_t *entry)
{
	entry->previous = *tail;
	entry->next = NULL;

	if (*tail != NULL) {
		(*tail)->next = entry;
	} else {
		*head = entry;
	}

	*tail = entry;
}

static void task_group_entry_list_remove(async_task_group_entry_t **head,
										 async_task_group_entry_t **tail,
										 async_task_group_entry_t *entry)
{
	if (entry->previous != NULL) {
		entry->previous->next = entry->next;
	} else {
		*head = entry->next;
	}

	if (entry->next != NULL) {
		entry->next->previous = entry->previous;
	} else {
		*tail = entry->previous;
	}

	entry->previous = NULL;
	entry->next = NULL;
}

/* A task key is a string or an integer; these use it on the tasks and on the arrays the reads build. */
static zval *task_group_key_find(const HashTable *table, const zval *key)
{
	return Z_TYPE_P(key) == IS_STRING ? zend_hash_find(table, Z_STR_P(key))
									  : zend_hash_index_find(table, Z_LVAL_P(key));
}

static void task_group_key_add(HashTable *table, const zval *key, zval *value)
{
	if (Z_TYPE_P(key) == IS_STRING) {
		zend_hash_add_new(table, Z_STR_P(key), value);
	} else {
		zend_hash_index_add_new(table, Z_LVAL_P(key), value);
	}
}

static void task_group_key_del(HashTable *table, const zval *key)
{
	if (Z_TYPE_P(key) == IS_STRING) {
		zend_hash_del(table, Z_STR_P(key));
	} else {
		zend_hash_index_del(table, Z_LVAL_P(key));
	}
}

static void task_group_throw_duplicate(const zval *key)
{
	if (Z_TYPE_P(key) == IS_STRING) {
		zend_throw_exception_ex(
				async_ce_async_exception, 0, "Duplicate key \"%s\" in TaskGroup", ZSTR_VAL(Z_STR_P(key)));
	} else {
		zend_throw_exception_ex(
				async_ce_async_exception, 0, "Duplicate key " ZEND_LONG_FMT " in TaskGroup", Z_LVAL_P(key));
	}
}

/* A new entry under `key`, which is not present, at the end of the spawn order. */
static async_task_group_entry_t *task_group_entry_add(async_task_group_t *group, const zval *key)
{
	async_task_group_entry_t *entry = ecalloc(1, sizeof(async_task_group_entry_t));
	zval pointer;

	ZVAL_COPY(&entry->key, key);
	ZVAL_UNDEF(&entry->result);
	ZVAL_PTR(&pointer, entry);
	task_group_key_add(&group->tasks, key, &pointer);

	return entry;
}

/* What a queued call holds besides the trampoline, which the start hands to the coroutine. */
static void task_group_call_release(zend_fcall_info *fci, zend_fcall_info_cache *fcc)
{
	zval_ptr_dtor(&fci->function_name);
	ZVAL_UNDEF(&fci->function_name);

	for (uint32_t i = 0; i < fci->param_count; i++) {
		zval_ptr_dtor(&fci->params[i]);
	}

	if (fci->params != NULL) {
		efree(fci->params);
		fci->params = NULL;
	}

	fci->param_count = 0;

	if (fci->named_params != NULL) {
		zend_array_release(fci->named_params);
		fci->named_params = NULL;
	}

	zend_object *object = fcc->object;
	zend_object *closure = fcc->closure;

	*fcc = empty_fcall_info_cache;

	if (object != NULL) {
		OBJ_RELEASE(object);
	}

	if (closure != NULL) {
		OBJ_RELEASE(closure);
	}
}

/* A queued call that never starts lets go of its trampoline too. */
static void task_group_call_release_unstarted(zend_fcall_info *fci, zend_fcall_info_cache *fcc)
{
	zend_release_fcall_info_cache(fcc);
	task_group_call_release(fci, fcc);
}

static void task_group_entry_free(zval *pointer)
{
	async_task_group_entry_t *entry = Z_PTR_P(pointer);

	switch (entry->state) {
		case TASK_QUEUED:
			task_group_call_release_unstarted(&entry->fci, &entry->fcc);
			break;
		case TASK_RUNNING:
			/* The group goes first only when a bailout skipped its destructor: the subscriber stays in the
			 * coroutine's callbacks, detached. */
			entry->subscriber->group = NULL;
			OBJ_RELEASE(&entry->coroutine->std);
			break;
		case TASK_SUCCEEDED:
			zval_ptr_dtor(&entry->result);
			break;
		case TASK_FAILED:
			OBJ_RELEASE(entry->exception);
			break;
	}

	zval_ptr_dtor(&entry->key);
	efree(entry);
}

/* A queued entry keeps its own counted copy of the callable and the arguments (section 2). */
static void task_group_entry_queue(async_task_group_t *group,
								   async_task_group_entry_t *entry,
								   const zend_fcall_info *fci,
								   const zend_fcall_info_cache *fcc,
								   const zval *args,
								   const uint32_t args_count,
								   HashTable *named_args)
{
	entry->state = TASK_QUEUED;
	entry->fci = *fci;
	Z_TRY_ADDREF(entry->fci.function_name);
	entry->fci.retval = NULL;
	entry->fci.params = NULL;
	entry->fci.param_count = 0;
	entry->fci.named_params = NULL;

	if (args_count != 0) {
		entry->fci.params = safe_emalloc(args_count, sizeof(zval), 0);
		entry->fci.param_count = args_count;

		for (uint32_t i = 0; i < args_count; i++) {
			ZVAL_COPY(&entry->fci.params[i], &args[i]);
		}
	}

	if (named_args != NULL) {
		GC_ADDREF(named_args);
		entry->fci.named_params = named_args;
	}

	/* Copies the engine's shared trampoline, which the next call through it would overwrite. */
	entry->fcc = *fcc;
	zend_fcc_addref(&entry->fcc);

	task_group_entry_list_append(&group->queued_head, &group->queued_tail, entry);
	group->queued_count++;
}

/* The entry's outcome as owned references for a Future. */
static void task_group_entry_outcome(const async_task_group_entry_t *entry, zval *value, zend_object **exception)
{
	ZEND_ASSERT(entry->state == TASK_SUCCEEDED || entry->state == TASK_FAILED);

	ZVAL_UNDEF(value);
	*exception = NULL;

	if (entry->state == TASK_SUCCEEDED) {
		ZVAL_COPY(value, &entry->result);
	} else {
		*exception = entry->exception;
		GC_ADDREF(*exception);
	}
}

/* A TaskSet's read delivered the entry: it leaves the set. */
static void task_group_entry_take(async_task_group_t *group, async_task_group_entry_t *entry)
{
	task_group_entry_list_remove(&group->settled_head, &group->settled_tail, entry);

	zval key;

	/* A copy: the delete frees the entry. */
	ZVAL_COPY(&key, &entry->key);
	task_group_key_del(&group->tasks, &key);
	zval_ptr_dtor(&key);
}

///////////////////////////////////////////////////////////////////
/// Futures
///////////////////////////////////////////////////////////////////

static void task_group_future_waiters_remove(async_task_group_futures_t *futures, const uint32_t index)
{
	memmove(&futures->waiters[index],
			&futures->waiters[index + 1],
			(futures->length - index - 1) * sizeof(async_task_group_future_waiter_t *));
	futures->length--;
}

/* The Future settled or went: the waiter leaves `futures` unless the group took it out first or went. */
static void task_group_future_waiter_dispose(async_event_callback_t *callback, async_awaitable_t *target)
{
	(void) target;

	async_task_group_future_waiter_t *const waiter = (async_task_group_future_waiter_t *) callback;
	async_task_group_t *const group = waiter->group;

	if (group != NULL) {
		async_task_group_futures_t *futures = &group->futures;

		for (uint32_t i = 0; i < futures->length; i++) {
			if (futures->waiters[i] == waiter) {
				task_group_future_waiters_remove(futures, i);
				break;
			}
		}
	}

	efree(waiter);
}

/* The group goes: its pending Futures stay pending, their waiters forget it. */
static void task_group_future_waiters_detach(async_task_group_futures_t *futures)
{
	for (uint32_t i = 0; i < futures->length; i++) {
		futures->waiters[i]->group = NULL;
	}

	if (futures->waiters != NULL) {
		efree(futures->waiters);
		futures->waiters = NULL;
	}

	futures->length = 0;
	futures->capacity = 0;
}

/* Records a pending read of `future` at the end of `futures`. */
static void task_group_future_waiter_add(async_task_group_t *group,
										 async_future_event_t *future,
										 const read_kind_t kind,
										 const bool ignore_errors)
{
	async_task_group_futures_t *futures = &group->futures;

	if (futures->length == futures->capacity) {
		futures->capacity = futures->capacity == 0 ? TASK_GROUP_FUTURES_FIRST_CAPACITY : futures->capacity * 2;
		futures->waiters =
				safe_erealloc(futures->waiters, futures->capacity, sizeof(async_task_group_future_waiter_t *), 0);
	}

	async_callbacks_reserve(&future->base.callbacks, 1);

	async_task_group_future_waiter_t *waiter = emalloc(sizeof(async_task_group_future_waiter_t));

	waiter->on_future.flags = 0;
	waiter->on_future.callback = async_callback_ignore;
	waiter->on_future.dispose = task_group_future_waiter_dispose;
	waiter->group = group;
	waiter->future = future;
	waiter->kind = kind;
	waiter->ignore_errors = ignore_errors;

	async_callbacks_push_reserved(&future->base.callbacks, &waiter->on_future);
	futures->waiters[futures->length++] = waiter;
}

/* Completes `future` with the owned `value` or `exception`. The future event lives through the resolve, which
 * disposes its waiter. */
static void task_group_future_settle(async_future_event_t *future, zval *value, zend_object *exception)
{
	future->base.ref_count++;
	async_future_event_resolve(future, exception == NULL ? value : NULL, exception);

	if (exception != NULL) {
		OBJ_RELEASE(exception);
	} else {
		zval_ptr_dtor(value);
	}

	async_future_event_release(future);
}

static void task_group_future_reject(async_future_event_t *future, const char *message)
{
	task_group_future_settle(future, NULL, async_new_exception(async_ce_async_exception, "%s", message));
}

zend_object *async_task_group_of_future_waiter(const async_event_callback_t *subscriber)
{
	/* A wait record's union holds its kind, not a dispose. */
	if (EXPECTED((subscriber->flags & ASYNC_CALLBACK_F_RECORD) ||
				 subscriber->dispose != task_group_future_waiter_dispose)) {
		return NULL;
	}

	const async_task_group_t *group = ((const async_task_group_future_waiter_t *) subscriber)->group;

	return group != NULL ? (zend_object *) &group->std : NULL;
}

void async_task_group_collector_sources(async_collector_t *collector, zend_object *group_object)
{
	async_task_group_t *const group = task_group_from_object(group_object);
	bool is_new_node;
	const uint32_t node = async_collector_reach_node(collector, &group->base, &is_new_node);

	/* Once per run: every pass meets the group again. */
	if (EXPECTED(!is_new_node)) {
		return;
	}

	async_collector_report_reach_to_object(collector, node, group_object);

	const async_task_group_entry_t *entry;

	ZEND_HASH_FOREACH_PTR(&group->tasks, entry)
	{
		if (entry->state == TASK_RUNNING) {
			async_collector_report_reach_source(collector, &entry->coroutine->std, node);
		}
	}
	ZEND_HASH_FOREACH_END();
}

///////////////////////////////////////////////////////////////////
/// The TASK_GROUP wait kind
///////////////////////////////////////////////////////////////////

/* Bits of a TASK_GROUP record's flags. A spawn() parked on a full queue waits in `slot_waiters`; a foreach step
 * and, with COMPLETION, an awaitCompletion() caller in `waiters` (section 4). */
#define TASK_GROUP_RECORD_F_SPAWNER (1u << ASYNC_CALLBACK_F_KIND_SHIFT)
#define TASK_GROUP_RECORD_F_COMPLETION (1u << (ASYNC_CALLBACK_F_KIND_SHIFT + 1))
/* The iterator lives in a C local the collector's walk does not read (async_collector_iterator_is_c_local()):
 * the wait owns its reference to the group, as a step releases its previous pair before it waits. */
#define TASK_GROUP_RECORD_F_HOLDS_GROUP (1u << (ASYNC_CALLBACK_F_KIND_SHIFT + 2))
/* A spawner woken to take the room there is, counted in `passed_spawners` until it runs. */
#define TASK_GROUP_RECORD_F_PASSED_ROOM (1u << (ASYNC_CALLBACK_F_KIND_SHIFT + 3))

static zend_always_inline async_task_group_t *task_group_of_record(const async_coroutine_event_callback_t *record)
{
	return (async_task_group_t *) record->event;
}

static zend_always_inline async_wait_queue_t *task_group_queue_of_role(async_task_group_t *group, const uint32_t role)
{
	return (role & TASK_GROUP_RECORD_F_SPAWNER) ? &group->slot_waiters : &group->waiters;
}

static zend_string *task_group_record_info(const async_coroutine_event_callback_t *record)
{
	const async_task_group_t *const group = task_group_of_record(record);
	const uint32_t flags = record->event_callback.flags;
	const char *wait = "foreach";

	if (UNEXPECTED(flags & TASK_GROUP_RECORD_F_SPAWNER)) {
		wait = "spawn() on a full queue";
	} else if (UNEXPECTED(flags & TASK_GROUP_RECORD_F_COMPLETION)) {
		wait = "awaitCompletion()";
	}

	return zend_strpprintf(0,
						   "%s(total=%u, active=%u, queued=%u): %s",
						   task_group_is_task_set(group) ? "TaskSet" : "TaskGroup",
						   zend_hash_num_elements(&group->tasks),
						   group->active_count,
						   group->queued_count,
						   wait);
}

/* The wake's enqueue unlinks the waiter's wait (D26): the record leaves its queue. So do a cancel's, a bailout's
 * and a suspend's refusal. */
static void task_group_record_unlink(async_coroutine_event_callback_t *record)
{
	async_task_group_t *const group = task_group_of_record(record);
	const bool is_removed =
			async_wait_queue_remove(task_group_queue_of_role(group, record->event_callback.flags), record);

	ZEND_ASSERT(is_removed && "a linked record is in its group's queue");
	(void) is_removed;
	record->event = NULL;
}

/* Whoever holds the group can spawn, close or cancel, which wakes every waiter, and a running task wakes them
 * as it ends (section 6). */
static void task_group_record_collector_target(const async_coroutine_event_callback_t *record,
											   async_collector_t *collector)
{
	async_task_group_t *const group = task_group_of_record(record);

	async_collector_report_target(
			collector, &group->std, (record->event_callback.flags & TASK_GROUP_RECORD_F_HOLDS_GROUP) != 0);
	async_task_group_collector_sources(collector, &group->std);
}

static const async_wait_kind_t task_group_wait_kind = {
	.info = task_group_record_info,
	.unlink = task_group_record_unlink,
	.collector_target = task_group_record_collector_target,
};

/* Parks the current coroutine in its role's queue (`role`: the record's bits) until a wake; false with an
 * exception. The caller has refused the scheduler context and checks the group again after any wake, as
 * TrueAsync's loops do (task_group.c:1153-1249, 1461-1486, 1823-1857). A parked wait allocates nothing (D29). */
static bool task_group_wait(async_task_group_t *group, const uint32_t role)
{
	async_coroutine_t *const waiter = (async_coroutine_t *) ZEND_ASYNC_CURRENT_COROUTINE;

	if (UNEXPECTED(waiter == NULL)) {
		zend_throw_error(NULL, "There is no coroutine to suspend");
		return false;
	}

	async_wait_queue_t *const queue = task_group_queue_of_role(group, role);
	async_coroutine_event_callback_t *const record = &waiter->waker.records[0];

	async_wait_end(waiter);
	async_wait_queue_make_room(queue);
	async_wait_link_outside(record, waiter, (async_awaitable_t *) &group->base, &task_group_wait_kind);
	record->event_callback.flags |= role;
	async_wait_queue_push(queue, record);

	ZEND_ASYNC_SUSPEND();
	ZEND_ASSERT(record->event == NULL && "every way back from the park unlinks the wait");

	if (UNEXPECTED(record->event_callback.flags & TASK_GROUP_RECORD_F_PASSED_ROOM)) {
		group->passed_spawners--;
	}

	return EG(exception) == NULL;
}

static void task_group_record_wake(const async_coroutine_event_callback_t *record)
{
#ifdef TRUE_ASYNC_TEST_HOOKS
	async_collector_check_event_wake(record->coroutine);
#endif

	async_scheduler_enqueue(&record->coroutine->coroutine, NULL, false);
}

/* Wakes every record of `queue` that carries none of the `skipped` bits, oldest first; each wake takes its
 * record out of the queue. */
static void task_group_wake(async_wait_queue_t *queue, const uint32_t skipped)
{
	uint32_t i = 0;

	while (i < queue->length) {
		const async_coroutine_event_callback_t *const record = queue->records[i];

		if (UNEXPECTED(record->event_callback.flags & skipped)) {
			i++;
			continue;
		}

		task_group_record_wake(record);

		/* An enqueue the scheduler refused left the record in place. */
		if (UNEXPECTED(i < queue->length && queue->records[i] == record)) {
			i++;
		}
	}
}

/* The foreach steps after an entry settled; at the completion every waiter. */
static void task_group_wake_waiters(async_task_group_t *group)
{
	task_group_wake(&group->waiters, (group->base.flags & TASK_GROUP_F_COMPLETED) ? 0 : TASK_GROUP_RECORD_F_COMPLETION);
}

static zend_always_inline bool task_group_queue_is_full(const async_task_group_t *group)
{
	return group->queue_limit != 0 && group->queued_count >= group->queue_limit;
}

/* The free slots and queue places together: a start from the queue moves a task between them. A spawner parks
 * only while both limits are set. */
static uint32_t task_group_free_room(const async_task_group_t *group)
{
	const uint32_t free_slots = group->concurrency > group->active_count ? group->concurrency - group->active_count : 0;
	const uint32_t free_places =
			group->queue_limit > group->queued_count ? group->queue_limit - group->queued_count : 0;

	return free_slots + free_places;
}

/* Wakes the oldest parked spawners, one for each free room no spawner woken before has yet taken: after a queued
 * task starts, and as a spawner leaves the park, by going on or by an exception, so no spawner stays parked with
 * room free (section 8, item 19). Until they run, newcomers wait behind them. */
static void task_group_pass_room(async_task_group_t *group)
{
	while (group->slot_waiters.length > 0 && task_group_free_room(group) > group->passed_spawners) {
		async_coroutine_event_callback_t *const record = group->slot_waiters.records[0];

		task_group_record_wake(record);

		/* An enqueue the scheduler refused left the record parked. */
		if (UNEXPECTED(record->event != NULL)) {
			return;
		}

		record->event_callback.flags |= TASK_GROUP_RECORD_F_PASSED_ROOM;
		group->passed_spawners++;
	}
}

/* close(), cancel(), dispose() and the destructor: spawn() is refused from now on, and every parked spawner
 * wakes to throw so (section 4). */
static void task_group_seal(async_task_group_t *group)
{
	group->base.flags |= TASK_GROUP_F_SEALED;
	task_group_wake(&group->slot_waiters, 0);
}

///////////////////////////////////////////////////////////////////
/// Answers
///////////////////////////////////////////////////////////////////

/* Every entry present needs no report: a successful race() or any() was made for the first result. */
static void task_group_mark_all_handled(async_task_group_t *group)
{
	async_task_group_entry_t *entry;

	ZEND_HASH_FOREACH_PTR(&group->tasks, entry)
	{
		entry->is_handled = true;
	}
	ZEND_HASH_FOREACH_END();
}

/* The errors present in spawn order as one CompositeException, or NULL when there is none. A read takes every
 * error and marks it handled; the closing's report (`only_unreported`) takes those neither handled nor a
 * cancellation (section 3). */
static zend_object *task_group_composite(async_task_group_t *group, const bool only_unreported)
{
	zend_object *composite = NULL;
	async_task_group_entry_t *entry;

	ZEND_HASH_FOREACH_PTR(&group->tasks, entry)
	{
		if (entry->state != TASK_FAILED) {
			continue;
		}

		if (only_unreported &&
			(entry->is_handled || instanceof_function(entry->exception->ce, async_ce_cancellation))) {
			continue;
		}

		if (composite == NULL) {
			composite = async_new_exception(async_ce_composite_exception, "");
		}

		async_composite_exception_add_exception(composite, entry->exception);

		if (!only_unreported) {
			entry->is_handled = true;
		}
	}
	ZEND_HASH_FOREACH_END();

	return composite;
}

/* Takes every entry of a settled TaskSet, which joinAll() delivered, into `taken`, for the caller to destroy once
 * the read is answered: the release of what the entries hold may run destructors, which find the set empty. */
static void task_group_take_all(async_task_group_t *group, HashTable *taken)
{
	ZEND_ASSERT(task_group_is_settled(group));

	*taken = group->tasks;
	zend_hash_init(&group->tasks, 8, NULL, task_group_entry_free, false);
	group->settled_head = NULL;
	group->settled_tail = NULL;
}

/* all() and joinAll() of a settled group (task_group.c:1554-1593): the results by key in spawn order, or a
 * CompositeException of the errors, which then need no report; `ignore_errors` leaves them out, handled too. */
static void task_group_answer_all(async_task_group_t *group, async_future_event_t *future, const bool ignore_errors)
{
	zend_object *composite = ignore_errors ? NULL : task_group_composite(group, false);
	zval results;

	ZVAL_UNDEF(&results);

	if (composite == NULL) {
		async_task_group_entry_t *entry;

		array_init(&results);

		ZEND_HASH_FOREACH_PTR(&group->tasks, entry)
		{
			if (entry->state == TASK_FAILED) {
				entry->is_handled = true;
				continue;
			}

			zval value;

			ZVAL_COPY(&value, &entry->result);
			task_group_key_add(Z_ARRVAL(results), &entry->key, &value);
		}
		ZEND_HASH_FOREACH_END();
	}

	if (!task_group_is_task_set(group)) {
		task_group_future_settle(future, &results, composite);
		return;
	}

	HashTable taken;

	task_group_take_all(group, &taken);
	task_group_future_settle(future, &results, composite);
	zend_hash_destroy(&taken);
}

/* A TaskGroup's race() or any() answered with `entry`: its own outcome. A success needs no report for any entry,
 * a failure none for its own. */
static void
task_group_answer_entry(async_task_group_t *group, async_future_event_t *future, async_task_group_entry_t *entry)
{
	zval value;
	zend_object *exception;

	task_group_entry_outcome(entry, &value, &exception);

	if (exception == NULL) {
		task_group_mark_all_handled(group);
	} else {
		entry->is_handled = true;
	}

	task_group_future_settle(future, &value, exception);
}

/* A TaskSet's read delivers `entry` and takes it. */
static void
task_group_answer_take(async_task_group_t *group, async_future_event_t *future, async_task_group_entry_t *entry)
{
	zval value;
	zend_object *exception;

	task_group_entry_outcome(entry, &value, &exception);
	task_group_entry_take(group, entry);
	task_group_future_settle(future, &value, exception);
}

/* The first entry in spawn order that has ended, or NULL. */
static async_task_group_entry_t *task_group_first_settled_in_spawn_order(const async_task_group_t *group,
																		 const bool only_successful)
{
	async_task_group_entry_t *entry;

	ZEND_HASH_FOREACH_PTR(&group->tasks, entry)
	{
		if (entry->state == TASK_SUCCEEDED || (!only_successful && entry->state == TASK_FAILED)) {
			return entry;
		}
	}
	ZEND_HASH_FOREACH_END();

	return NULL;
}

/* The first successful entry in completion order, or NULL. */
static async_task_group_entry_t *task_group_first_success_in_completion_order(const async_task_group_t *group)
{
	for (async_task_group_entry_t *entry = group->settled_head; entry != NULL; entry = entry->next) {
		if (entry->state == TASK_SUCCEEDED) {
			return entry;
		}
	}

	return NULL;
}

/* A TaskSet's joinAny() on a settled set with failures only: a CompositeException of them, which it takes. */
static void task_group_answer_set_any_failed(async_task_group_t *group, async_future_event_t *future)
{
	zend_object *composite = task_group_composite(group, false);
	HashTable taken;

	task_group_take_all(group, &taken);
	task_group_future_settle(future, NULL, composite);
	zend_hash_destroy(&taken);
}

/* Whether the waiter's read can be answered now; `entry` gets the entry it is answered with, NULL for an answer
 * from every entry. */
static bool task_group_read_target(const async_task_group_t *group,
								   const async_task_group_future_waiter_t *waiter,
								   async_task_group_entry_t **entry)
{
	*entry = NULL;

	switch (waiter->kind) {
		case READ_ALL:
			return task_group_is_settled(group);
		case READ_RACE:
			/* A pending race() found nothing settled at its call, and a TaskGroup takes no entry: the head
			 * of the settled list is the first task to end since. */
			*entry = group->settled_head;

			return *entry != NULL;
		case READ_ANY:
			*entry = task_group_first_success_in_completion_order(group);

			return *entry != NULL || (task_group_is_settled(group) && group->settled_head != NULL);
	}

	return false;
}

/* Answers the waiter, out of `futures`, with the `entry` task_group_read_target() gave. */
static void
task_group_answer(async_task_group_t *group, async_task_group_future_waiter_t *waiter, async_task_group_entry_t *entry)
{
	async_future_event_t *const future = waiter->future;
	const bool is_set = task_group_is_task_set(group);

	waiter->group = NULL;

	switch (waiter->kind) {
		case READ_ALL:
			task_group_answer_all(group, future, waiter->ignore_errors);
			break;
		case READ_RACE:
			if (is_set) {
				task_group_answer_take(group, future, entry);
			} else {
				task_group_answer_entry(group, future, entry);
			}

			break;
		case READ_ANY:
			if (entry == NULL && is_set) {
				task_group_answer_set_any_failed(group, future);
			} else if (entry == NULL) {
				task_group_future_settle(future, NULL, task_group_composite(group, false));
			} else if (is_set) {
				task_group_answer_take(group, future, entry);
			} else {
				task_group_answer_entry(group, future, entry);
			}

			break;
	}
}

/* Answers every pending read that can be answered now, oldest first: each gets its own answer, where
 * TrueAsync's loop skips the waiter after each one it settles (task_group.c:665-717). */
static void task_group_serve(async_task_group_t *group)
{
	uint32_t i = 0;

	while (i < group->futures.length) {
		async_task_group_future_waiter_t *const waiter = group->futures.waiters[i];
		async_task_group_entry_t *entry;

		if (!task_group_read_target(group, waiter, &entry)) {
			i++;
			continue;
		}

		task_group_future_waiters_remove(&group->futures, i);
		task_group_answer(group, waiter, entry);
	}
}

/* A TaskSet's pending joinNext() and joinAny() with nothing left to take: the empty set's messages (section 3). */
static void task_group_reject_set_reads(async_task_group_t *group)
{
	if (!task_group_is_task_set(group)) {
		return;
	}

	uint32_t i = 0;

	while (i < group->futures.length) {
		async_task_group_future_waiter_t *waiter = group->futures.waiters[i];

		if (waiter->kind == READ_ALL) {
			i++;
			continue;
		}

		task_group_future_waiters_remove(&group->futures, i);
		waiter->group = NULL;
		task_group_future_reject(waiter->future,
								 waiter->kind == READ_RACE ? task_group_empty_race_message
														   : task_group_empty_any_message);
	}
}

/* After a change of the tasks: the reads that can be answered are, and a sealed group with nothing left
 * completes. */
static void task_group_update(async_task_group_t *group)
{
	task_group_serve(group);

	if (task_group_is_settled(group) && (group->base.flags & TASK_GROUP_F_SEALED) &&
		!(group->base.flags & TASK_GROUP_F_COMPLETED)) {
		task_group_complete(group);
	}
}

///////////////////////////////////////////////////////////////////
/// Tasks
///////////////////////////////////////////////////////////////////

/* Inside the coroutine's notify, in scheduler context: only records the outcome. The group keeps the error,
 * so it does not take the scope's route (S9-scope.md 4). */
static void
task_group_task_ended(async_awaitable_t *target, async_event_callback_t *callback, void *result, zend_object *exception)
{
	task_subscriber_t *const subscriber = (task_subscriber_t *) callback;
	async_task_group_entry_t *const entry = subscriber->entry;

	if (UNEXPECTED(subscriber->group == NULL || entry->state != TASK_RUNNING)) {
		return;
	}

	if (exception != NULL) {
		((async_coroutine_t *) target)->coroutine.flags |= ASYNC_COROUTINE_F_EXCEPTION_HANDLED;
		entry->state = TASK_FAILED;
		entry->exception = exception;
		GC_ADDREF(exception);
	} else {
		entry->state = TASK_SUCCEEDED;
		ZVAL_COPY(&entry->result, (zval *) result);
	}
}

static void task_group_drain(async_task_group_t *group);
static void task_group_cancel(async_task_group_t *group, zend_object *cancellation);

/* After the notify, outside scheduler context and before the coroutine leaves the scope (task_group.c:891-998):
 * the entry settles, the queue drains, the reads are answered and the group may complete. A dispose whose
 * callback did not run, because an earlier callback threw and ended the notify, reads the outcome from the
 * coroutine. After a bailout nothing more runs. */
static void task_group_task_ended_dispose(async_event_callback_t *callback, async_awaitable_t *target)
{
	task_subscriber_t *const subscriber = (task_subscriber_t *) callback;
	async_task_group_t *const group = subscriber->group;
	async_task_group_entry_t *const entry = subscriber->entry;

	efree(subscriber);

	if (UNEXPECTED(group == NULL)) {
		return;
	}

	async_coroutine_t *const coroutine = (async_coroutine_t *) target;
	zend_coroutine_t *const zend_coroutine = &coroutine->coroutine;

	if (UNEXPECTED(entry->state == TASK_RUNNING)) {
		if (zend_coroutine->exception != NULL) {
			/* The finalize reads this after the dispose: the group keeps the error. */
			zend_coroutine->flags |= ASYNC_COROUTINE_F_EXC_CAUGHT;
			entry->state = TASK_FAILED;
			entry->exception = zend_coroutine->exception;
			GC_ADDREF(entry->exception);
		} else {
			entry->state = TASK_SUCCEEDED;
			ZVAL_COPY(&entry->result, &zend_coroutine->result);
		}
	}

	entry->subscriber = NULL;
	entry->coroutine = NULL;
	group->active_count--;
	task_group_entry_list_append(&group->settled_head, &group->settled_tail, entry);

	/* The finalize holds the coroutine through the dispose. */
	OBJ_RELEASE(&coroutine->std);

	if (UNEXPECTED(zend_coroutine->flags & ASYNC_COROUTINE_F_BAILOUT)) {
		return;
	}

	zend_object *saved_exception = NULL;

	async_exception_save_fast(&EG(exception), &saved_exception);
	GC_ADDREF(&group->std);

	task_group_drain(group);
	task_group_update(group);
	task_group_wake_waiters(group);

	OBJ_RELEASE(&group->std);
	async_exception_restore_fast(&EG(exception), &saved_exception);
}

/* The entry runs `coroutine`, whose reference it takes: a subscriber goes into the coroutine's callbacks. */
static void
task_group_entry_run(async_task_group_t *group, async_task_group_entry_t *entry, async_coroutine_t *coroutine)
{
	task_subscriber_t *subscriber = emalloc(sizeof(task_subscriber_t));

	subscriber->callback.flags = 0;
	subscriber->callback.callback = task_group_task_ended;
	subscriber->callback.dispose = task_group_task_ended_dispose;
	subscriber->group = group;
	subscriber->entry = entry;

	async_callbacks_reserve(&coroutine->callbacks, 1);
	async_callbacks_push_reserved(&coroutine->callbacks, &subscriber->callback);

	entry->state = TASK_RUNNING;
	entry->coroutine = coroutine;
	entry->subscriber = subscriber;
	group->active_count++;
}

/* Whether the scope takes no more tasks: gone, closed, or cancelled, which a spawn would not refuse
 * (src/scope.c, async_scope_spawn) but the group does (section 5). */
static zend_always_inline bool task_group_scope_is_stopped(const async_scope_t *scope)
{
	return scope == NULL || (scope->event.flags & (ASYNC_SCOPE_F_CLOSED | ASYNC_SCOPE_F_CANCELLED));
}

/* Starts queued tasks from the head while slots are free. A scope cancelled or closed from outside starts
 * nothing: at the first task end that finds it so, queue or not, the group cancels as cancel() does, which wakes
 * its waiters (section 5; TrueAsync's drain starts them, task_group.c:848-889). */
static void task_group_drain(async_task_group_t *group)
{
	while (true) {
		async_scope_t *const scope = task_group_scope(group);

		/* Checked again after each release, which may run a destructor. */
		if (UNEXPECTED(task_group_scope_is_stopped(scope) &&
					   !(group->base.flags & (TASK_GROUP_F_COMPLETED | TASK_GROUP_F_CANCELLED)))) {
			task_group_cancel(group, async_new_exception(async_ce_cancellation, "TaskGroup cancelled"));
			return;
		}

		if (EXPECTED(group->queued_head == NULL || !task_group_has_slot(group))) {
			return;
		}

		async_task_group_entry_t *entry = group->queued_head;

		task_group_entry_list_remove(&group->queued_head, &group->queued_tail, entry);
		group->queued_count--;

		/* The coroutine takes the trampoline, whether it starts or the scheduler refuses it. */
		async_coroutine_t *coroutine = async_scope_spawn(scope,
														 NULL,
														 &entry->fci,
														 &entry->fcc,
														 entry->fci.params,
														 entry->fci.param_count,
														 entry->fci.named_params);

		if (EXPECTED(coroutine != NULL)) {
			task_group_entry_run(group, entry, coroutine);
		} else {
			/* The scheduler could not make a stack: the refusal is the task's outcome. */
			entry->state = TASK_FAILED;
			entry->exception = EG(exception);
			GC_ADDREF(entry->exception);
			zend_clear_exception();
			task_group_entry_list_append(&group->settled_head, &group->settled_tail, entry);
		}

		/* The place it left in the queue goes to the oldest parked spawner (section 4). */
		task_group_pass_room(group);

		/* After the entry has its new state: a destructor the release runs finds the group as it is. */
		task_group_call_release(&entry->fci, &entry->fcc);
	}
}

/* cancel(), dispose(), the destructor's and a stopped scope's cancel, with the owned `cancellation`: the group
 * seals, the queued tasks end with it unstarted, the reads are answered, and the scope is cancelled, never
 * safely, so the running tasks are interrupted (section 5). Marks no error handled. A second cancel and a cancel
 * of a completed group do nothing. */
static void task_group_cancel(async_task_group_t *group, zend_object *cancellation)
{
	if (UNEXPECTED(group->base.flags & (TASK_GROUP_F_COMPLETED | TASK_GROUP_F_CANCELLED))) {
		OBJ_RELEASE(cancellation);
		return;
	}

	group->base.flags |= TASK_GROUP_F_CANCELLED;
	task_group_seal(group);
	GC_ADDREF(&group->std);

	/* The queued entries end first, and their calls are released after the answers: the release may run
	 * destructors, which then find every entry settled. */
	const uint32_t unstarted_count = group->queued_count;
	task_call_t *unstarted = unstarted_count > 0 ? safe_emalloc(unstarted_count, sizeof(task_call_t), 0) : NULL;

	for (uint32_t i = 0; i < unstarted_count; i++) {
		async_task_group_entry_t *entry = group->queued_head;

		task_group_entry_list_remove(&group->queued_head, &group->queued_tail, entry);
		group->queued_count--;
		unstarted[i].fci = entry->fci;
		unstarted[i].fcc = entry->fcc;
		entry->fci = empty_fcall_info;
		entry->fcc = empty_fcall_info_cache;
		entry->state = TASK_FAILED;
		entry->exception = cancellation;
		GC_ADDREF(cancellation);
		task_group_entry_list_append(&group->settled_head, &group->settled_tail, entry);
	}

	ZEND_ASSERT(group->queued_head == NULL);
	task_group_serve(group);

	async_scope_t *scope = task_group_scope(group);

	/* A scope cancelled from outside has cancelled the tasks already; a second cancel would close it and start its
	 * finally handlers while they unwind (src/scope.c:672). */
	if (EXPECTED(scope != NULL && !(scope->event.flags & (ASYNC_SCOPE_F_CANCELLED | ASYNC_SCOPE_F_CLOSED)))) {
		async_scope_cancel(scope, cancellation, true, false);
	} else {
		OBJ_RELEASE(cancellation);
	}

	task_group_update(group);

	if (EXPECTED(unstarted_count > 0)) {
		task_group_wake_waiters(group);
	}

	for (uint32_t i = 0; i < unstarted_count; i++) {
		task_group_call_release_unstarted(&unstarted[i].fci, &unstarted[i].fcc);
	}

	if (unstarted != NULL) {
		efree(unstarted);
	}

	OBJ_RELEASE(&group->std);
}

///////////////////////////////////////////////////////////////////
/// Completion and closing
///////////////////////////////////////////////////////////////////

static void task_group_closing_subscriber_dispose(async_event_callback_t *callback, async_awaitable_t *target)
{
	(void) target;

	async_task_group_t *const group = ((closing_subscriber_t *) callback)->group;

	efree(callback);
	task_group_closing_end(group, false);
}

/* The finally run ends. The closing ends here once the destructor ran; a worker that never ran ends the run
 * inside its notify, in scheduler context, so the closing ends at the subscriber's dispose after it (section 5). */
static void task_group_finally_run_end(zend_object *target)
{
	async_task_group_t *const group = task_group_from_object(target);

	group->base.flags &= ~TASK_GROUP_F_FINALLY_RUNNING;

	if (!(group->base.flags & TASK_GROUP_F_CLOSING)) {
		return;
	}

	async_coroutine_t *const coroutine = (async_coroutine_t *) ZEND_ASYNC_CURRENT_COROUTINE;

	if (UNEXPECTED(ZEND_ASYNC_IN_SCHEDULER_CONTEXT && coroutine != NULL &&
				   (coroutine->callbacks.capacity & ASYNC_CALLBACKS_F_NOTIFYING))) {
		closing_subscriber_t *subscriber = emalloc(sizeof(closing_subscriber_t));

		subscriber->callback.flags = 0;
		subscriber->callback.callback = async_callback_ignore;
		subscriber->callback.dispose = task_group_closing_subscriber_dispose;
		subscriber->group = group;

		async_callbacks_reserve(&coroutine->callbacks, 1);
		async_callbacks_push_reserved(&coroutine->callbacks, &subscriber->callback);
		return;
	}

	task_group_closing_end(group, false);
}

/* Sealed with nothing left (task_group.c:720-746): the TaskSet reads with nothing to take reject, and the
 * finally handlers start once, in a coroutine of the group's scope behind the coroutines already queued, with
 * the group as their argument; TrueAsync starts them at every settle (section 8, item 13). Without handlers, or
 * with a refused run, which releases them unrun, a closing group ends its closing here. */
static void task_group_complete(async_task_group_t *group)
{
	group->base.flags |= TASK_GROUP_F_COMPLETED;
	task_group_reject_set_reads(group);
	task_group_wake_waiters(group);

	HashTable *finally_handlers = group->finally_handlers;

	if (UNEXPECTED(finally_handlers != NULL)) {
		group->finally_handlers = NULL;

		async_scope_t *scope = task_group_scope(group);

		if (scope == NULL) {
			scope = async_scope_current();
		}

		if (EXPECTED(async_finally_handlers_start(
					finally_handlers, scope, &group->std, false, task_group_finally_run_end))) {
			group->base.flags |= TASK_GROUP_F_FINALLY_RUNNING;
			return;
		}

		zend_array_release(finally_handlers);

		/* A scheduler that could not make the run's coroutine left its exception pending: the closing only
		 * releases, as after a bailout (section 5), since it would refuse the reporter and the cancels' work
		 * too. */
		if (UNEXPECTED(EG(exception) != NULL)) {
			if (group->base.flags & TASK_GROUP_F_CLOSING) {
				task_group_closing_end(group, true);
			}

			return;
		}
	}

	if (group->base.flags & TASK_GROUP_F_CLOSING) {
		task_group_closing_end(group, false);
	}
}

/* Whether a coroutine of the scope's subtree still runs: a task's child, or another coroutine of an external
 * scope. The finishing task is FINISHED already, and a finally run's scope, the closing's own among them, is
 * left out. */
static bool task_group_scope_has_work(const async_scope_t *scope)
{
	for (uint32_t i = 0; i < scope->coroutines.length; i++) {
		if (!ZEND_COROUTINE_IS_FINISHED(&scope->coroutines.data[i]->coroutine)) {
			return true;
		}
	}

	for (uint32_t i = 0; i < scope->child_scopes.length; i++) {
		const async_scope_t *child_scope = scope->child_scopes.data[i];

		if (!(child_scope->event.flags & ASYNC_SCOPE_F_FINALLY_RUN) && task_group_scope_has_work(child_scope)) {
			return true;
		}
	}

	return false;
}

/* The reporter's body throws the composite its coroutine holds, which then takes the scope's route: a
 * handler on the way, else Uncaught (TrueAsync's async_spawn_and_throw, exceptions.c:335-365). */
static void task_group_reporter_entry(void)
{
	zend_coroutine_t *const reporter = ZEND_ASYNC_CURRENT_COROUTINE;
	zend_object *const composite = reporter->extended_data;

	reporter->extended_data = NULL;
	zend_throw_exception_internal(composite);
}

/* A reporter that never ran (a bailout, the request's end) lets go of its composite. */
static void task_group_reporter_dispose(zend_coroutine_t *reporter)
{
	zend_object *composite = reporter->extended_data;

	if (composite != NULL) {
		reporter->extended_data = NULL;
		OBJ_RELEASE(composite);
	}
}

/* Reports the owned `composite` from a reporter coroutine in a new child scope of `parent_scope` (section 5,
 * step 5): protected, so a later cancel of the scope does not finish it unrun, and at the front of the queue, as
 * TrueAsync's at priority 1. The graceful shutdown would cancel it unrun, so then the composite goes with the
 * exit's exceptions; with async off it goes with the request. */
static void task_group_report(zend_object *composite, async_scope_t *parent_scope)
{
	if (UNEXPECTED(!ZEND_ASYNC_IS_ACTIVE)) {
		OBJ_RELEASE(composite);
		return;
	}

	if (UNEXPECTED(ASYNC_G(graceful_shutdown))) {
		async_exit_exception_add(composite);
		OBJ_RELEASE(composite);
		return;
	}

	async_scope_t *scope = async_scope_new(parent_scope);
	async_coroutine_t *reporter = async_coroutine_new();

	reporter->coroutine.internal_entry = task_group_reporter_entry;
	reporter->coroutine.extended_data = composite;
	reporter->coroutine.extended_dispose = task_group_reporter_dispose;
	reporter->coroutine.flags |= ASYNC_COROUTINE_F_PROTECTED | ASYNC_COROUTINE_F_HI_PRIORITY;
	async_scope_add_coroutine(scope, reporter);

	if (UNEXPECTED(!async_scheduler_enqueue(&reporter->coroutine, NULL, false))) {
		async_scope_discard_coroutine(reporter);
	}
}

/* The end of the closing (section 5, steps 5-7): the scope is cancelled while a coroutine of it still runs,
 * the errors nobody saw are reported, the own scope's pin and the external Scope object go, and the closing's
 * reference. After a bailout, once the request's end freed the scopes, or when the caller says so (a refused
 * finally run's exception pending), it only releases. A pending exception, a destructor's on the way here, waits
 * aside: the scope calls would stop at it. */
static void task_group_closing_end(async_task_group_t *group, const bool caller_release_only)
{
	if (UNEXPECTED(group->base.flags & TASK_GROUP_F_CLOSING_ENDED)) {
		return;
	}

	group->base.flags |= TASK_GROUP_F_CLOSING_ENDED;

	const bool is_release_only = caller_release_only || CG(unclean_shutdown) || ASYNC_G(global_scope) == NULL;

	zend_object *saved_exception = NULL;

	async_exception_save_fast(&EG(exception), &saved_exception);

	if (EXPECTED(!is_release_only)) {
		async_scope_t *scope = task_group_scope(group);

		if (scope != NULL && task_group_scope_has_work(scope)) {
			zend_object *cancellation =
					async_new_exception(async_ce_cancellation, "Scope is being disposed due to TaskGroup destruction");

			async_scope_cancel_remaining(scope, cancellation);
			OBJ_RELEASE(cancellation);
		}

		zend_object *composite = task_group_composite(group, true);

		if (UNEXPECTED(composite != NULL)) {
			/* Read again: the cancel may have freed an external scope. */
			scope = task_group_scope(group);
			task_group_report(composite, scope != NULL ? scope : async_scope_current());
		}
	}

	async_scope_t *own_scope = group->scope;

	if (own_scope != NULL) {
		group->scope = NULL;

		if (is_release_only) {
			async_scope_forget_owner(own_scope);
		} else {
			async_scope_release_owner(own_scope);
		}
	}

	zend_object *scope_object = group->scope_object;

	if (scope_object != NULL) {
		group->scope_object = NULL;
		OBJ_RELEASE(scope_object);
	}

	/* After the release too: the group's free may run destructors. */
	OBJ_RELEASE(&group->std);
	async_exception_restore_fast(&EG(exception), &saved_exception);
}

void async_task_group_scope_freed(zend_object *group_object)
{
	task_group_from_object(group_object)->scope = NULL;
}

///////////////////////////////////////////////////////////////////
/// Objects
///////////////////////////////////////////////////////////////////

static zend_object *task_group_object_create(zend_class_entry *class_entry)
{
	async_task_group_t *group = zend_object_alloc(sizeof(async_task_group_t), class_entry);

	memset(group, 0, offsetof(async_task_group_t, std));
	async_event_init_in_object(&group->base,
							   class_entry == async_ce_task_set ? TASK_GROUP_F_TASK_SET : 0,
							   offsetof(async_task_group_t, std));
	zend_hash_init(&group->tasks, 8, NULL, task_group_entry_free, false);

	zend_object_std_init(&group->std, class_entry);
	object_properties_init(&group->std, class_entry);

	return &group->std;
}

/* The destructor closes the group and never waits (section 5): it takes the object again for the closing,
 * seals, rejects the TaskSet reads with nothing to take, then completes a settled group or cancels the rest
 * with TrueAsync's message, marking nothing handled. The closing ends after the completion's finally run. */
static void task_group_object_destroy(zend_object *object)
{
	async_task_group_t *const group = task_group_from_object(object);

	if (UNEXPECTED(!(group->base.flags & TASK_GROUP_F_CONSTRUCTED))) {
		return;
	}

	zend_object *saved_exception = NULL;

	async_exception_save_fast(&EG(exception), &saved_exception);

	GC_ADDREF(object);
	group->base.flags |= TASK_GROUP_F_CLOSING;
	task_group_seal(group);
	task_group_reject_set_reads(group);

	if (group->base.flags & TASK_GROUP_F_COMPLETED) {
		if (!(group->base.flags & TASK_GROUP_F_FINALLY_RUNNING)) {
			task_group_closing_end(group, false);
		}
	} else if (task_group_is_settled(group)) {
		task_group_complete(group);
	} else if (group->base.flags & TASK_GROUP_F_CANCELLED) {
		/* A cancel ran: the completion comes at the last task's end. */
		task_group_update(group);
	} else {
		task_group_cancel(
				group,
				async_new_exception(async_ce_cancellation, "Scope is being disposed due to TaskGroup destruction"));
	}

	async_exception_restore_fast(&EG(exception), &saved_exception);
}

/* zend_object_std_dtor first, as every object of the extension (S9.21). Never reads the own scope through
 * anything but the field the request's end clears (src/scope.c, scope_free). */
static void task_group_object_free(zend_object *object)
{
	async_task_group_t *const group = task_group_from_object(object);

	zend_object_std_dtor(object);
	task_group_future_waiters_detach(&group->futures);
	async_wait_queue_free(&group->slot_waiters);
	async_wait_queue_free(&group->waiters);
	group->queued_head = NULL;
	group->queued_tail = NULL;
	group->settled_head = NULL;
	group->settled_tail = NULL;
	zend_hash_destroy(&group->tasks);

	if (UNEXPECTED(group->finally_handlers != NULL)) {
		zend_array_release(group->finally_handlers);
		group->finally_handlers = NULL;
	}

	if (group->scope != NULL) {
		async_scope_forget_owner(group->scope);
		group->scope = NULL;
	}

	if (group->scope_object != NULL) {
		zend_object *const scope_object = group->scope_object;

		group->scope_object = NULL;
		OBJ_RELEASE(scope_object);
	}
}

/* What TrueAsync's reports (task_group.c:555-604): the queued callables and arguments, the running coroutines,
 * the results, the exceptions, the finally handlers and the external Scope object. */
static HashTable *task_group_object_gc(zend_object *object, zval **table, int *num)
{
	async_task_group_t *const group = task_group_from_object(object);
	zend_get_gc_buffer *gc_buffer = zend_get_gc_buffer_create();
	async_task_group_entry_t *entry;

	ZEND_HASH_FOREACH_PTR(&group->tasks, entry)
	{
		switch (entry->state) {
			case TASK_QUEUED:
				zend_get_gc_buffer_add_zval(gc_buffer, &entry->fci.function_name);

				for (uint32_t i = 0; i < entry->fci.param_count; i++) {
					zend_get_gc_buffer_add_zval(gc_buffer, &entry->fci.params[i]);
				}

				if (entry->fci.named_params != NULL) {
					zend_get_gc_buffer_add_ht(gc_buffer, entry->fci.named_params);
				}

				zend_get_gc_buffer_add_fcc(gc_buffer, &entry->fcc);
				break;
			case TASK_RUNNING:
				zend_get_gc_buffer_add_obj(gc_buffer, &entry->coroutine->std);
				break;
			case TASK_SUCCEEDED:
				zend_get_gc_buffer_add_zval(gc_buffer, &entry->result);
				break;
			case TASK_FAILED:
				zend_get_gc_buffer_add_obj(gc_buffer, entry->exception);
				break;
		}
	}
	ZEND_HASH_FOREACH_END();

	if (group->finally_handlers != NULL) {
		zend_get_gc_buffer_add_ht(gc_buffer, group->finally_handlers);
	}

	if (group->scope_object != NULL) {
		zend_get_gc_buffer_add_obj(gc_buffer, group->scope_object);
	}

	zend_get_gc_buffer_use(gc_buffer, table, num);

	return NULL;
}

///////////////////////////////////////////////////////////////////
/// The iterator
///////////////////////////////////////////////////////////////////

/* foreach yields `key => [$result, null]` or `key => [null, $error]` in completion order and waits while no
 * entry is left to yield until a task ends, or ends with the completed group (section 3; TrueAsync's walks the
 * spawn order, task_group.c:1143-1250). `data` holds the group. */
typedef struct
{
	zend_object_iterator iterator;
	/* A TaskGroup's: the last entry yielded, NULL before the first. The group keeps its entries while the
	 * iterator holds it; a TaskSet's step takes the oldest settled entry instead. */
	async_task_group_entry_t *last_entry;
	zval key;
	zval current; /* UNDEF before the first entry and after the last */
	bool started;
} task_group_iterator_t;

static void task_group_iterator_dtor(zend_object_iterator *zend_iterator)
{
	task_group_iterator_t *const iterator = (task_group_iterator_t *) zend_iterator;

	zval_ptr_dtor(&iterator->current);
	zval_ptr_dtor(&iterator->key);
	zval_ptr_dtor(&zend_iterator->data);
}

static zend_result task_group_iterator_valid(zend_object_iterator *zend_iterator)
{
	return Z_ISUNDEF(((task_group_iterator_t *) zend_iterator)->current) ? FAILURE : SUCCESS;
}

static zval *task_group_iterator_current(zend_object_iterator *zend_iterator)
{
	return &((task_group_iterator_t *) zend_iterator)->current;
}

static void task_group_iterator_key(zend_object_iterator *zend_iterator, zval *key)
{
	ZVAL_COPY(key, &((task_group_iterator_t *) zend_iterator)->key);
}

/* The next entry to yield, or NULL. */
static async_task_group_entry_t *task_group_iterator_next(const task_group_iterator_t *iterator,
														  const async_task_group_t *group)
{
	if (UNEXPECTED(task_group_is_task_set(group) || iterator->last_entry == NULL)) {
		return group->settled_head;
	}

	return iterator->last_entry->next;
}

/* Stores the entry's pair and key; its error needs no report from now on, and a TaskSet's entry leaves it. */
static void
task_group_iterator_yield(task_group_iterator_t *iterator, async_task_group_t *group, async_task_group_entry_t *entry)
{
	zval value;
	zend_object *exception;

	task_group_entry_outcome(entry, &value, &exception);
	array_init_size(&iterator->current, 2);

	if (EXPECTED(exception == NULL)) {
		add_next_index_zval(&iterator->current, &value);
		add_next_index_null(&iterator->current);
	} else {
		add_next_index_null(&iterator->current);
		add_next_index_object(&iterator->current, exception);
	}

	ZVAL_COPY(&iterator->key, &entry->key);
	entry->is_handled = true;

	if (UNEXPECTED(task_group_is_task_set(group))) {
		task_group_entry_take(group, entry);
	} else {
		iterator->last_entry = entry;
	}
}

static void task_group_iterator_move_forward(zend_object_iterator *zend_iterator)
{
	task_group_iterator_t *const iterator = (task_group_iterator_t *) zend_iterator;
	async_task_group_t *const group = task_group_from_object(Z_OBJ(zend_iterator->data));

	/* Taken out before their release: a destructor that steps the same iterator, or suspends so that another
	 * coroutine's step runs meanwhile, would release them again. */
	zval previous_value;
	zval previous_key;

	ZVAL_COPY_VALUE(&previous_value, &iterator->current);
	ZVAL_COPY_VALUE(&previous_key, &iterator->key);
	ZVAL_UNDEF(&iterator->current);
	ZVAL_UNDEF(&iterator->key);
	zval_ptr_dtor(&previous_value);
	zval_ptr_dtor(&previous_key);

	/* The previous entry's destructor threw: a step now would yield an entry the loop never sees. */
	if (UNEXPECTED(EG(exception) != NULL)) {
		return;
	}

	if (UNEXPECTED(async_throw_if_unavailable())) {
		return;
	}

	const uint32_t role = async_collector_iterator_is_c_local() ? TASK_GROUP_RECORD_F_HOLDS_GROUP : 0;

	while (true) {
		async_task_group_entry_t *const entry = task_group_iterator_next(iterator, group);

		if (EXPECTED(entry != NULL)) {
			/* A destructor's own step or another coroutine's step may have stored an entry meanwhile. */
			ZVAL_COPY_VALUE(&previous_value, &iterator->current);
			ZVAL_COPY_VALUE(&previous_key, &iterator->key);
			task_group_iterator_yield(iterator, group, entry);
			zval_ptr_dtor(&previous_value);
			zval_ptr_dtor(&previous_key);
			return;
		}

		if (UNEXPECTED(group->base.flags & TASK_GROUP_F_COMPLETED)) {
			return;
		}

		if (UNEXPECTED(!task_group_wait(group, role))) {
			return;
		}
	}
}

static HashTable *task_group_iterator_gc(zend_object_iterator *zend_iterator, zval **table, int *num)
{
	task_group_iterator_t *const iterator = (task_group_iterator_t *) zend_iterator;
	zend_get_gc_buffer *const gc_buffer = zend_get_gc_buffer_create();

	zend_get_gc_buffer_add_zval(gc_buffer, &zend_iterator->data);
	zend_get_gc_buffer_add_zval(gc_buffer, &iterator->key);
	zend_get_gc_buffer_add_zval(gc_buffer, &iterator->current);
	zend_get_gc_buffer_use(gc_buffer, table, num);

	return NULL;
}

/* Starts the loop once: a second foreach over the same iterator goes on where the first stopped. A loop that
 * starts takes responsibility for the errors present: they need no report (section 3). */
static void task_group_iterator_rewind(zend_object_iterator *zend_iterator)
{
	task_group_iterator_t *const iterator = (task_group_iterator_t *) zend_iterator;

	if (UNEXPECTED(iterator->started)) {
		return;
	}

	/* A refused loop delivered nothing, so its errors stay reported. */
	if (UNEXPECTED(async_throw_if_unavailable())) {
		return;
	}

	iterator->started = true;

	const async_task_group_t *const group = task_group_from_object(Z_OBJ(zend_iterator->data));
	async_task_group_entry_t *entry;

	ZEND_HASH_FOREACH_PTR(&group->tasks, entry)
	{
		if (UNEXPECTED(entry->state == TASK_FAILED)) {
			entry->is_handled = true;
		}
	}
	ZEND_HASH_FOREACH_END();

	task_group_iterator_move_forward(zend_iterator);
}

static const zend_object_iterator_funcs task_group_iterator_funcs = {
	.dtor = task_group_iterator_dtor,
	.valid = task_group_iterator_valid,
	.get_current_data = task_group_iterator_current,
	.get_current_key = task_group_iterator_key,
	.move_forward = task_group_iterator_move_forward,
	.rewind = task_group_iterator_rewind,
	.get_gc = task_group_iterator_gc,
};

static zend_object_iterator *task_group_get_iterator(zend_class_entry *class_entry, zval *object, int by_ref)
{
	(void) class_entry;

	if (UNEXPECTED(by_ref)) {
		zend_throw_error(NULL, "Cannot iterate TaskGroup by reference");
		return NULL;
	}

	async_task_group_t *const group = task_group_from_object(Z_OBJ_P(object));
	task_group_iterator_t *const iterator = emalloc(sizeof(task_group_iterator_t));

	zend_iterator_init(&iterator->iterator);
	iterator->iterator.funcs = &task_group_iterator_funcs;
	ZVAL_OBJ_COPY(&iterator->iterator.data, &group->std);
	iterator->last_entry = NULL;
	ZVAL_UNDEF(&iterator->key);
	ZVAL_UNDEF(&iterator->current);
	iterator->started = false;

	return &iterator->iterator;
}

///////////////////////////////////////////////////////////////////
/// Methods
///////////////////////////////////////////////////////////////////

ZEND_METHOD(Async_TaskGroup, __construct)
{
	zend_long concurrency = 0;
	bool concurrency_is_null = true;
	zend_long queue_limit = 0;
	bool queue_limit_is_null = true;
	zend_object *scope_object = NULL;

	ZEND_PARSE_PARAMETERS_START(0, 3)
		Z_PARAM_OPTIONAL
		Z_PARAM_LONG_OR_NULL(concurrency, concurrency_is_null)
		Z_PARAM_LONG_OR_NULL(queue_limit, queue_limit_is_null)
		Z_PARAM_OBJ_OF_CLASS_OR_NULL(scope_object, async_ce_scope)
	ZEND_PARSE_PARAMETERS_END();

	async_task_group_t *const group = THIS_GROUP;

	/* TrueAsync's second construction swaps the scope (section 8, item 17). */
	if (UNEXPECTED(group->base.flags & TASK_GROUP_F_CONSTRUCTED)) {
		zend_throw_error(NULL, "Cannot call constructor twice");
		RETURN_THROWS();
	}

	if (UNEXPECTED(!concurrency_is_null && (concurrency < 0 || (zend_ulong) concurrency > UINT32_MAX))) {
		zend_argument_value_error(1, "must be between 0 and %u", UINT32_MAX);
		RETURN_THROWS();
	}

	if (UNEXPECTED(!queue_limit_is_null && (queue_limit < 0 || (zend_ulong) queue_limit > UINT32_MAX))) {
		zend_argument_value_error(2, "must be between 0 and %u", UINT32_MAX);
		RETURN_THROWS();
	}

	const uint32_t concurrency_limit = concurrency_is_null ? 0 : (uint32_t) concurrency;

	group->concurrency = concurrency_limit;

	/* 2 * $concurrency by default, saturated; no limit without a concurrency limit (task_group.c:1375-1389). */
	if (!queue_limit_is_null) {
		group->queue_limit = (uint32_t) queue_limit;
	} else if (concurrency_limit > UINT32_MAX / 2) {
		group->queue_limit = UINT32_MAX;
	} else {
		group->queue_limit = concurrency_limit * 2;
	}

	if (scope_object == NULL) {
		/* The own scope does not dispose safely (section 8, item 10): a task's error that takes the scope's route
		 * cancels the running tasks, and the child scopes the tasks make inherit that. */
		async_scope_t *scope = async_scope_new(async_scope_current());

		scope->event.flags &= ~ASYNC_SCOPE_F_DISPOSE_SAFELY;
		scope->owner_object = &group->std;
		group->scope = scope;
		group->base.flags |= TASK_GROUP_F_CONSTRUCTED;
		return;
	}

	async_scope_t *scope = async_scope_object_from_object(scope_object)->scope;

	if (EXPECTED(scope != NULL) && UNEXPECTED(scope->event.flags & ASYNC_SCOPE_F_ZOMBIES_ALLOWED)) {
		/* Before the object is taken: a handler that throws leaves nothing to release. */
		php_error_docref(NULL, E_WARNING, "TaskGroup cancels its tasks even though the Scope allows zombies");

		if (UNEXPECTED(EG(exception) != NULL)) {
			RETURN_THROWS();
		}

		/* The handler may have disposed it. */
		scope = async_scope_object_from_object(scope_object)->scope;
	}

	if (UNEXPECTED(scope == NULL)) {
		zend_throw_exception(async_ce_async_exception, "Cannot use a disposed Scope for TaskGroup", 0);
		RETURN_THROWS();
	}

	GC_ADDREF(scope_object);
	group->scope_object = scope_object;
	group->base.flags |= TASK_GROUP_F_CONSTRUCTED;
}

/* spawn(), spawnWithKey(), trySpawn() and trySpawnWithKey() (task_group.c:1421-1507; section 2). A try that
 * finds no free slot, or a spawner parked or woken ahead of it, queues nothing and takes no integer key; spawn()
 * takes one at its call, as TrueAsync's, and parks on a full queue (section 4). */
static void task_group_spawn_method(INTERNAL_FUNCTION_PARAMETERS, const bool with_key, const bool is_try)
{
	zend_string *key_string = NULL;
	zend_long key_long = 0;
	zend_fcall_info fci;
	zend_fcall_info_cache fcc;
	zval *args = NULL;
	uint32_t args_count = 0;
	HashTable *named_args = NULL;

	/* Before the parsing: a refusal after it would have to release the trampoline. */
	THROW_IF_UNAVAILABLE();

	if (with_key) {
		ZEND_PARSE_PARAMETERS_START(2, -1)
			Z_PARAM_STR_OR_LONG(key_string, key_long)
			Z_PARAM_FUNC_NO_TRAMPOLINE_FREE(fci, fcc)
			Z_PARAM_VARIADIC_WITH_NAMED(args, args_count, named_args)
		ZEND_PARSE_PARAMETERS_END();
	} else {
		ZEND_PARSE_PARAMETERS_START(1, -1)
			Z_PARAM_FUNC_NO_TRAMPOLINE_FREE(fci, fcc)
			Z_PARAM_VARIADIC_WITH_NAMED(args, args_count, named_args)
		ZEND_PARSE_PARAMETERS_END();
	}

	async_task_group_t *const group = THIS_GROUP;
	zval key;
	zend_ulong key_index;

	/* A numeric string is the integer key, as in a PHP array: the reads build arrays with these keys. */
	if (with_key && key_string != NULL &&
		ZEND_HANDLE_NUMERIC_STR(ZSTR_VAL(key_string), ZSTR_LEN(key_string), key_index)) {
		ZVAL_LONG(&key, (zend_long) key_index);
	} else if (with_key && key_string != NULL) {
		ZVAL_STR(&key, key_string);
	} else if (with_key) {
		ZVAL_LONG(&key, key_long);
	} else if (is_try) {
		ZVAL_LONG(&key, group->next_key);
	} else {
		ZVAL_LONG(&key, group->next_key++);
	}

	/* A spawner parked on a full queue checks everything again when it wakes (task_group.c:1461-1486). */
	bool has_parked = false;

	while (true) {
		/* The seal woke every parked spawner. */
		if (UNEXPECTED(group->base.flags & TASK_GROUP_F_SEALED)) {
			zend_release_fcall_info_cache(&fcc);
			zend_throw_exception(async_ce_async_exception, task_group_closed_message, 0);
			break;
		}

		if (UNEXPECTED(task_group_key_find(&group->tasks, &key) != NULL)) {
			/* A trySpawn() takes the taken integer as spawn() does, so the next call moves on. */
			if (UNEXPECTED(is_try && !with_key)) {
				group->next_key++;
			}

			zend_release_fcall_info_cache(&fcc);
			task_group_throw_duplicate(&key);
			break;
		}

		async_scope_t *scope = task_group_scope(group);

		/* A scope cancelled or closed from outside: the group seals and cancels (section 5). */
		if (UNEXPECTED(task_group_scope_is_stopped(scope))) {
			const bool is_cancelled = scope != NULL && (scope->event.flags & ASYNC_SCOPE_F_CANCELLED);

			zend_release_fcall_info_cache(&fcc);
			task_group_cancel(group, async_new_exception(async_ce_cancellation, "TaskGroup cancelled"));

			if (EXPECTED(EG(exception) == NULL)) {
				zend_throw_exception(async_ce_async_exception,
									 is_cancelled ? task_group_closed_message
												  : "Cannot spawn a coroutine in a closed scope",
									 0);
			}

			break;
		}

		/* A newcomer takes no room ahead of a spawner parked or woken to take it (DECISIONS 2026-10-09 S9.29). */
		const bool waits_its_turn = !has_parked && (group->slot_waiters.length > 0 || group->passed_spawners > 0);

		if (EXPECTED(!waits_its_turn && task_group_has_slot(group))) {
			async_coroutine_t *coroutine = async_scope_spawn(scope, NULL, &fci, &fcc, args, args_count, named_args);

			if (UNEXPECTED(coroutine == NULL)) {
				break;
			}

			if (UNEXPECTED(is_try && !with_key)) {
				group->next_key++;
			}

			task_group_entry_run(group, task_group_entry_add(group, &key), coroutine);

			if (UNEXPECTED(is_try)) {
				RETVAL_TRUE;
			}

			break;
		}

		if (UNEXPECTED(is_try)) {
			zend_release_fcall_info_cache(&fcc);
			RETURN_FALSE;
		}

		if (EXPECTED(!waits_its_turn && !task_group_queue_is_full(group))) {
			task_group_entry_queue(group, task_group_entry_add(group, &key), &fci, &fcc, args, args_count, named_args);
			break;
		}

		const bool is_woken = task_group_wait(group, TASK_GROUP_RECORD_F_SPAWNER);

		has_parked = true;

		if (UNEXPECTED(!is_woken)) {
			zend_release_fcall_info_cache(&fcc);
			break;
		}
	}

	/* Leaving the park, by going on or by an exception, passes on the room left. */
	if (UNEXPECTED(has_parked)) {
		task_group_pass_room(group);
	}
}

ZEND_METHOD(Async_TaskGroup, spawn)
{
	task_group_spawn_method(INTERNAL_FUNCTION_PARAM_PASSTHRU, false, false);
}

ZEND_METHOD(Async_TaskGroup, spawnWithKey)
{
	task_group_spawn_method(INTERNAL_FUNCTION_PARAM_PASSTHRU, true, false);
}

ZEND_METHOD(Async_TaskGroup, trySpawn)
{
	task_group_spawn_method(INTERNAL_FUNCTION_PARAM_PASSTHRU, false, true);
}

ZEND_METHOD(Async_TaskGroup, trySpawnWithKey)
{
	task_group_spawn_method(INTERNAL_FUNCTION_PARAM_PASSTHRU, true, true);
}

/* all() and joinAll(): answered at once on a settled group. */
ZEND_METHOD(Async_TaskGroup, all)
{
	bool ignore_errors = false;

	ZEND_PARSE_PARAMETERS_START(0, 1)
		Z_PARAM_OPTIONAL
		Z_PARAM_BOOL(ignore_errors)
	ZEND_PARSE_PARAMETERS_END();

	async_task_group_t *const group = THIS_GROUP;
	async_future_event_t *future;
	zend_object *future_object = async_future_new_pending(&future);

	if (task_group_is_settled(group)) {
		task_group_answer_all(group, future, ignore_errors);
	} else {
		task_group_future_waiter_add(group, future, READ_ALL, ignore_errors);
	}

	RETURN_OBJ(future_object);
}

/* race(): the first entry settled at the call in spawn order (task_group.c:1595-1635); joinNext(): the first in
 * completion order, taken. Else pending until a task ends. */
ZEND_METHOD(Async_TaskGroup, race)
{
	ZEND_PARSE_PARAMETERS_NONE();

	async_task_group_t *const group = THIS_GROUP;

	if (UNEXPECTED(zend_hash_num_elements(&group->tasks) == 0)) {
		zend_throw_exception(async_ce_async_exception, task_group_empty_race_message, 0);
		RETURN_THROWS();
	}

	async_future_event_t *future;
	zend_object *future_object = async_future_new_pending(&future);

	/* A TaskSet's older pending reads take first. */
	if (task_group_is_task_set(group)) {
		task_group_future_waiter_add(group, future, READ_RACE, false);
		task_group_serve(group);
		RETURN_OBJ(future_object);
	}

	async_task_group_entry_t *entry = task_group_first_settled_in_spawn_order(group, false);

	if (entry != NULL) {
		task_group_answer_entry(group, future, entry);
	} else {
		task_group_future_waiter_add(group, future, READ_RACE, false);
	}

	RETURN_OBJ(future_object);
}

/* any(): the first successful entry in spawn order (task_group.c:1639-1682); joinAny(): in completion order,
 * taken. A settled group with failures only rejects; else pending. */
ZEND_METHOD(Async_TaskGroup, any)
{
	ZEND_PARSE_PARAMETERS_NONE();

	async_task_group_t *const group = THIS_GROUP;

	if (UNEXPECTED(zend_hash_num_elements(&group->tasks) == 0)) {
		zend_throw_exception(async_ce_async_exception, task_group_empty_any_message, 0);
		RETURN_THROWS();
	}

	async_future_event_t *future;
	zend_object *future_object = async_future_new_pending(&future);

	/* A TaskSet's older pending reads take first. */
	if (task_group_is_task_set(group)) {
		task_group_future_waiter_add(group, future, READ_ANY, false);
		task_group_serve(group);
		RETURN_OBJ(future_object);
	}

	async_task_group_entry_t *entry = task_group_first_settled_in_spawn_order(group, true);

	if (entry != NULL) {
		task_group_answer_entry(group, future, entry);
	} else if (task_group_is_settled(group)) {
		task_group_future_settle(future, NULL, task_group_composite(group, false));
	} else {
		task_group_future_waiter_add(group, future, READ_ANY, false);
	}

	RETURN_OBJ(future_object);
}

ZEND_METHOD(Async_TaskGroup, getResults)
{
	ZEND_PARSE_PARAMETERS_NONE();

	async_task_group_t *const group = THIS_GROUP;
	async_task_group_entry_t *entry;

	array_init(return_value);

	ZEND_HASH_FOREACH_PTR(&group->tasks, entry)
	{
		if (entry->state != TASK_SUCCEEDED) {
			continue;
		}

		Z_TRY_ADDREF(entry->result);
		task_group_key_add(Z_ARRVAL_P(return_value), &entry->key, &entry->result);
	}
	ZEND_HASH_FOREACH_END();
}

/* The errors present by key, which then need no report (task_group.c:1692-1699). */
ZEND_METHOD(Async_TaskGroup, getErrors)
{
	ZEND_PARSE_PARAMETERS_NONE();

	async_task_group_t *const group = THIS_GROUP;
	async_task_group_entry_t *entry;

	array_init(return_value);

	ZEND_HASH_FOREACH_PTR(&group->tasks, entry)
	{
		if (entry->state != TASK_FAILED) {
			continue;
		}

		zval error;

		entry->is_handled = true;
		ZVAL_OBJ_COPY(&error, entry->exception);
		task_group_key_add(Z_ARRVAL_P(return_value), &entry->key, &error);
	}
	ZEND_HASH_FOREACH_END();
}

ZEND_METHOD(Async_TaskGroup, suppressErrors)
{
	ZEND_PARSE_PARAMETERS_NONE();

	async_task_group_t *const group = THIS_GROUP;
	async_task_group_entry_t *entry;

	ZEND_HASH_FOREACH_PTR(&group->tasks, entry)
	{
		if (entry->state == TASK_FAILED) {
			entry->is_handled = true;
		}
	}
	ZEND_HASH_FOREACH_END();
}

ZEND_METHOD(Async_TaskGroup, cancel)
{
	zend_object *cancellation = NULL;

	ZEND_PARSE_PARAMETERS_START(0, 1)
		Z_PARAM_OPTIONAL
		Z_PARAM_OBJ_OF_CLASS_OR_NULL(cancellation, async_ce_cancellation)
	ZEND_PARSE_PARAMETERS_END();

	if (cancellation != NULL) {
		GC_ADDREF(cancellation);
	} else {
		cancellation = async_new_exception(async_ce_cancellation, "TaskGroup cancelled");
	}

	task_group_cancel(THIS_GROUP, cancellation);
}

/* Seals; a settled group completes. Queued and running tasks go on. */
ZEND_METHOD(Async_TaskGroup, close)
{
	ZEND_PARSE_PARAMETERS_NONE();

	async_task_group_t *const group = THIS_GROUP;

	task_group_seal(group);
	task_group_update(group);
}

/* cancel() with its own message; TrueAsync's leaves the group open (section 8, item 11). */
ZEND_METHOD(Async_TaskGroup, dispose)
{
	ZEND_PARSE_PARAMETERS_NONE();

	task_group_cancel(THIS_GROUP,
					  async_new_exception(async_ce_cancellation, "Scope is being disposed due to TaskGroup disposal"));
}

ZEND_METHOD(Async_TaskGroup, isFinished)
{
	ZEND_PARSE_PARAMETERS_NONE();

	RETURN_BOOL(task_group_is_settled(THIS_GROUP));
}

ZEND_METHOD(Async_TaskGroup, isClosed)
{
	ZEND_PARSE_PARAMETERS_NONE();

	RETURN_BOOL((THIS_GROUP->base.flags & TASK_GROUP_F_SEALED) != 0);
}

ZEND_METHOD(Async_TaskGroup, count)
{
	ZEND_PARSE_PARAMETERS_NONE();

	RETURN_LONG(zend_hash_num_elements(&THIS_GROUP->tasks));
}

/* Whether `coroutine` runs one of the group's tasks, which the group's completion waits for. */
static bool task_group_runs_task(const async_task_group_t *group, const zend_coroutine_t *coroutine)
{
	const async_task_group_entry_t *entry;

	ZEND_HASH_FOREACH_PTR(&group->tasks, entry)
	{
		if (UNEXPECTED(entry->state == TASK_RUNNING && &entry->coroutine->coroutine == coroutine)) {
			return true;
		}
	}
	ZEND_HASH_FOREACH_END();

	return false;
}

/* Waits for the tasks only, until the group completes (task_group.c:1807-1858): it never throws a task's error
 * and takes no cancellation token. Refused in scheduler context only when it would park, as Scope::awaitCompletion(),
 * and in a task of the group, which the completion waits for (TrueAsync parks it for good). */
ZEND_METHOD(Async_TaskGroup, awaitCompletion)
{
	ZEND_PARSE_PARAMETERS_NONE();

	async_task_group_t *const group = THIS_GROUP;

	if (UNEXPECTED(!(group->base.flags & TASK_GROUP_F_SEALED))) {
		zend_throw_exception(async_ce_async_exception, "TaskGroup must be closed before calling awaitCompletion()", 0);
		RETURN_THROWS();
	}

	if (UNEXPECTED(group->base.flags & TASK_GROUP_F_COMPLETED)) {
		return;
	}

	THROW_IF_UNAVAILABLE();

	if (UNEXPECTED(task_group_runs_task(group, ZEND_ASYNC_CURRENT_COROUTINE))) {
		zend_throw_exception(async_ce_async_exception, "Cannot await completion of TaskGroup from one of its tasks", 0);
		RETURN_THROWS();
	}

	do {
		if (UNEXPECTED(!task_group_wait(group, TASK_GROUP_RECORD_F_COMPLETION))) {
			RETURN_THROWS();
		}
	} while (!(group->base.flags & TASK_GROUP_F_COMPLETED));
}

/* Stored until the completion; on a completed group the callback is called at once, in the caller
 * (task_group.c:1860-1895). */
ZEND_METHOD(Async_TaskGroup, finally)
{
	zval *callback;

	ZEND_PARSE_PARAMETERS_START(1, 1)
		Z_PARAM_OBJECT_OF_CLASS(callback, zend_ce_closure)
	ZEND_PARSE_PARAMETERS_END();

	async_task_group_t *const group = THIS_GROUP;

	if (UNEXPECTED(group->base.flags & TASK_GROUP_F_COMPLETED)) {
		zval argument;
		zval retval;

		ZVAL_OBJ(&argument, &group->std);
		call_user_function(NULL, NULL, callback, &retval, 1, &argument);
		zval_ptr_dtor(&retval);
		return;
	}

	if (group->finally_handlers == NULL) {
		group->finally_handlers = zend_new_array(1);
	}

	Z_ADDREF_P(callback);
	zend_hash_next_index_insert_new(group->finally_handlers, callback);
}

/* TrueAsync's message (task_group.c:1904): only foreach gets an iterator. */
ZEND_METHOD(Async_TaskGroup, getIterator)
{
	ZEND_PARSE_PARAMETERS_NONE();

	zend_throw_error(NULL, "An object of class Async\\TaskGroup is not a traversable object in an invalid state");
}

void async_register_task_group_ce(void)
{
	async_ce_task_group = register_class_Async_TaskGroup(async_ce_awaitable, zend_ce_countable, zend_ce_aggregate);
	async_ce_task_group->create_object = task_group_object_create;
	async_ce_task_group->get_iterator = task_group_get_iterator;
	async_ce_task_group->default_object_handlers = &task_group_handlers;

	async_ce_task_set = register_class_Async_TaskSet(async_ce_awaitable, zend_ce_countable, zend_ce_aggregate);
	async_ce_task_set->create_object = task_group_object_create;
	async_ce_task_set->get_iterator = task_group_get_iterator;
	async_ce_task_set->default_object_handlers = &task_group_handlers;

	memcpy(&task_group_handlers, zend_get_std_object_handlers(), sizeof(zend_object_handlers));
	task_group_handlers.offset = offsetof(async_task_group_t, std);
	task_group_handlers.dtor_obj = task_group_object_destroy;
	task_group_handlers.free_obj = task_group_object_free;
	task_group_handlers.get_gc = task_group_object_gc;
	task_group_handlers.clone_obj = NULL;
}
