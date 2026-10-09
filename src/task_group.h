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
#ifndef TRUE_ASYNC_TASK_GROUP_H
#define TRUE_ASYNC_TASK_GROUP_H

#include "php.h"
#include "true_async_API.h"

/* Async\TaskGroup and Async\TaskSet (dev/plans/S9-taskgroup.md): callables run as coroutines of one scope,
 * at most `concurrency` at a time with the rest queued, their outcomes kept by key. One implementation
 * serves both classes; a TaskSet's reads take the entries they deliver. */

typedef struct _async_task_group_entry_s async_task_group_entry_t;
typedef struct _async_task_group_future_waiter_s async_task_group_future_waiter_t;

/* The pending Futures of the reads, oldest first. */
typedef struct
{
	async_task_group_future_waiter_t **waiters;
	uint32_t length;
	uint32_t capacity;
} async_task_group_futures_t;

typedef struct
{
	async_event_t base; /* the group's flags in its type bits; no subscribers */
	/* The group's own child scope, pinned (`owner_object`); NULL with an external scope, once the closing
	 * released it, or once the request's end freed it. */
	async_scope_t *scope;
	zend_object *scope_object; /* an external Async\Scope, held; its scope is read through it at each use */
	uint32_t concurrency;      /* 0: no limit */
	uint32_t queue_limit;      /* 0: no limit */
	uint32_t active_count;     /* running tasks */
	uint32_t queued_count;
	zend_long next_key; /* spawn()'s next integer key */
	HashTable tasks;    /* key => entry, in spawn order */
	/* The queued entries, oldest first, and the settled ones in the order they ended: an entry is in one of
	 * the two lists or in neither (running). */
	async_task_group_entry_t *queued_head;
	async_task_group_entry_t *queued_tail;
	async_task_group_entry_t *settled_head;
	async_task_group_entry_t *settled_tail;
	async_task_group_futures_t futures;
	/* The TASK_GROUP waits (section 4): spawn() parked on a full queue, oldest first; the foreach steps
	 * and the awaitCompletion() callers. */
	async_wait_queue_t slot_waiters;
	async_wait_queue_t waiters;
	uint32_t passed_spawners;    /* spawners woken to take the room there is, not yet run */
	HashTable *finally_handlers; /* lazy: the closures of finally() */
	zend_object std;
} async_task_group_t;

extern zend_class_entry *async_ce_task_group;
extern zend_class_entry *async_ce_task_set;

void async_register_task_group_ce(void);

/* For scope_free() (scope.c): the request's end frees the group's own scope, which the group then forgets. */
void async_task_group_scope_freed(zend_object *group_object);

/* The group object whose read `subscriber` waits for, else NULL: for the collector (collector.h), whoever
 * holds the group can settle the Future. */
zend_object *async_task_group_of_future_waiter(const async_event_callback_t *subscriber);

/* For the collector: the running tasks of `group_object` settle its Futures without a holder, so each one
 * that can run makes the group live, once per run. */
void async_task_group_collector_sources(async_collector_t *collector, zend_object *group_object);

#endif /* TRUE_ASYNC_TASK_GROUP_H */
