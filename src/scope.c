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
#include "Zend/zend_exceptions.h"
#include "Zend/zend_interfaces.h"
#include "Zend/zend_closures.h"
#include "php_true_async.h"
#include "scope.h"
#include "channel.h"
#include "exceptions.h"
#include "scheduler.h"
#include "await.h"
#include "future.h"
#include "collector.h"
#include "context.h"
#include "scope_arginfo.h"

zend_class_entry *async_ce_scope = NULL;
zend_class_entry *async_ce_scope_provider = NULL;
zend_class_entry *async_ce_spawn_strategy = NULL;

static zend_object_handlers scope_handlers;

///////////////////////////////////////////////////////////////////
/// Membership
///////////////////////////////////////////////////////////////////

static void scope_add_child(async_scope_t *parent_scope, async_scope_t *child_scope)
{
	async_scopes_vector_t *child_scopes = &parent_scope->child_scopes;

	if (child_scopes->length == child_scopes->capacity) {
		child_scopes->capacity = child_scopes->capacity == 0 ? 4 : child_scopes->capacity * 2;
		child_scopes->data = safe_erealloc(child_scopes->data, child_scopes->capacity, sizeof(async_scope_t *), 0);
	}

	child_scope->parent_scope = parent_scope;
	child_scope->child_index = child_scopes->length;
	child_scopes->data[child_scopes->length++] = child_scope;
}

static void scope_remove_child(async_scope_t *parent_scope, const async_scope_t *child_scope)
{
	async_scopes_vector_t *child_scopes = &parent_scope->child_scopes;

	ZEND_ASSERT(child_scopes->data[child_scope->child_index] == child_scope);

	async_scope_t *last_child_scope = child_scopes->data[--child_scopes->length];

	child_scopes->data[child_scope->child_index] = last_child_scope;
	last_child_scope->child_index = child_scope->child_index;
}

void async_scope_add_coroutine(async_scope_t *scope, async_coroutine_t *coroutine)
{
	async_coroutines_vector_t *coroutines = &scope->coroutines;

	ZEND_ASSERT(coroutine->scope == NULL);

	if (coroutines->length == coroutines->capacity) {
		coroutines->capacity = coroutines->capacity == 0 ? 4 : coroutines->capacity * 2;
		coroutines->data = safe_erealloc(coroutines->data, coroutines->capacity, sizeof(async_coroutine_t *), 0);
	}

	coroutine->scope = scope;
	coroutine->scope_index = coroutines->length;
	coroutines->data[coroutines->length++] = coroutine;

	if (UNEXPECTED(coroutine->coroutine.flags & ASYNC_COROUTINE_F_ZOMBIE)) {
		scope->zombie_coroutines_count++;
	} else {
		scope->active_coroutines_count++;
	}
}

/* The last member moves into the gap (section 9, item 1). */
static void scope_detach_coroutine(async_coroutine_t *coroutine)
{
	async_scope_t *scope = coroutine->scope;
	async_coroutines_vector_t *coroutines = &scope->coroutines;
	async_coroutine_t *last_coroutine = coroutines->data[--coroutines->length];

	ZEND_ASSERT(coroutines->data[coroutine->scope_index] == coroutine);

	coroutines->data[coroutine->scope_index] = last_coroutine;
	last_coroutine->scope_index = coroutine->scope_index;
	coroutine->scope = NULL;

	if (scope != ASYNC_G(global_scope)) {
		coroutine->coroutine.flags |= ASYNC_COROUTINE_F_LEFT_NON_GLOBAL_SCOPE;
	}

	if (UNEXPECTED(coroutine->coroutine.flags & ASYNC_COROUTINE_F_ZOMBIE)) {
		scope->zombie_coroutines_count--;
	} else {
		scope->active_coroutines_count--;
	}
}

static void scope_notify_completion(async_scope_t *scope, bool with_zombies, const async_coroutine_t *member);

void async_scope_mark_zombie(async_coroutine_t *coroutine)
{
	if (UNEXPECTED(coroutine->coroutine.flags & ASYNC_COROUTINE_F_ZOMBIE)) {
		return;
	}

	coroutine->coroutine.flags |= ASYNC_COROUTINE_F_ZOMBIE;
	ASYNC_G(zombie_coroutines_count)++;

	async_scope_t *scope = coroutine->scope;

	if (EXPECTED(scope != NULL)) {
		scope->active_coroutines_count--;
		scope->zombie_coroutines_count++;
		scope_notify_completion(scope, false, coroutine);
	}
}

async_scope_t *async_scope_current(void)
{
	const async_coroutine_t *coroutine = (async_coroutine_t *) ZEND_ASYNC_CURRENT_COROUTINE;

	return coroutine != NULL && coroutine->scope != NULL ? coroutine->scope : ASYNC_G(global_scope);
}

zend_object *async_scope_context(async_scope_t *scope)
{
	if (scope->context == NULL) {
		scope->context = async_context_new_for_scope(scope);
	}

	return scope->context;
}

///////////////////////////////////////////////////////////////////
/// States
///////////////////////////////////////////////////////////////////

/* No coroutine of the scope or of its children runs, zombies counted or not; a closed or cancelled
 * scope counts as completed whatever runs in it (TrueAsync's can_be_disposed without the object check,
 * scope.c:1503-1555): isFinished(), and cancel()'s test for the branch that closes the scope and passes
 * the cancel only to its idle child scopes. A `completed_child_scope` the caller found completed is not
 * walked again. */
static bool
scope_is_completed(const async_scope_t *scope, const bool with_zombies, const async_scope_t *completed_child_scope)
{
	if (scope->event.flags & (ASYNC_SCOPE_F_CLOSED | ASYNC_SCOPE_F_CANCELLED)) {
		return true;
	}

	const uint32_t running_count = scope->active_coroutines_count + (with_zombies ? scope->zombie_coroutines_count : 0);

	if (EXPECTED(running_count > 0)) {
		return false;
	}

	for (uint32_t i = 0; i < scope->child_scopes.length; i++) {
		const async_scope_t *child_scope = scope->child_scopes.data[i];

		if (child_scope != completed_child_scope && !scope_is_completed(child_scope, with_zombies, NULL)) {
			return false;
		}
	}

	return true;
}

/* A coroutine of the scope or of its child scopes has not finished, zombies included, whatever the
 * scope's state: what awaitAfterCancellation() waits for. */
static bool scope_has_coroutines(const async_scope_t *scope)
{
	if (scope->coroutines.length > 0) {
		return true;
	}

	for (uint32_t i = 0; i < scope->child_scopes.length; i++) {
		const async_scope_t *child_scope = scope->child_scopes.data[i];

		if (scope_has_coroutines(child_scope)) {
			return true;
		}
	}

	return false;
}

static bool scope_has_user_values(const async_scope_t *scope)
{
	return ZEND_FCC_INITIALIZED(scope->exception_handler) || ZEND_FCC_INITIALIZED(scope->child_exception_handler) ||
			scope->finally_handlers != NULL || scope->context != NULL;
}

/* Nothing can use the scope any more: no coroutine, zombies included, it is cancelled or its object is
 * gone, and every child scope is the same (TrueAsync's can_be_disposed with both checks). */
static bool scope_can_be_disposed(const async_scope_t *scope)
{
	if (EXPECTED(scope->active_coroutines_count + scope->zombie_coroutines_count > 0 ||
				 (scope->event.flags & ASYNC_SCOPE_F_REQUEST_LIFETIME))) {
		return false;
	}

	if (!(scope->event.flags & ASYNC_SCOPE_F_CANCELLED) && scope->scope_object != NULL) {
		return false;
	}

	for (uint32_t i = 0; i < scope->child_scopes.length; i++) {
		if (!scope_can_be_disposed(scope->child_scopes.data[i])) {
			return false;
		}
	}

	return true;
}

/* Only the object reaches the scope: destroying it would dispose the scope, at once or after its finally
 * handlers. A coroutine in the subtree reaches the handlers through the error route and the context through
 * current_context(), and a held child scope through its spawns and its context's find(), none of which an
 * object reports. A `disposable_child_scope` the caller found disposable is not walked again. */
static bool scope_is_reached_only_by_object(const async_scope_t *scope, const async_scope_t *disposable_child_scope)
{
	if (scope->active_coroutines_count + scope->zombie_coroutines_count > 0 ||
		(scope->event.flags & ASYNC_SCOPE_F_REQUEST_LIFETIME)) {
		return false;
	}

	for (uint32_t i = 0; i < scope->child_scopes.length; i++) {
		const async_scope_t *child_scope = scope->child_scopes.data[i];

		if (child_scope != disposable_child_scope && !scope_can_be_disposed(child_scope)) {
			return false;
		}
	}

	return true;
}

static void scope_set_cancelled(async_scope_t *scope)
{
	scope->event.flags |= ASYNC_SCOPE_F_CANCELLED;

	if (scope->scope_object != NULL) {
		async_scope_object_from_object(scope->scope_object)->is_cancelled = true;
	}
}

/* Wakes the waiters of the scope, then of each parent, while each has completed in turn (TrueAsync's
 * scope_check_completion_and_notify, scope.c:1575-1592), because `member` finished or became a zombie.
 * A wake only enqueues. */
static void scope_notify_completion(async_scope_t *scope, const bool with_zombies, const async_coroutine_t *member)
{
#ifndef TRUE_ASYNC_TEST_HOOKS
	(void) member;
#endif

	const async_scope_t *completed_child_scope = NULL;

	/* A parent walks only its other child scopes, so a chain of N nested scopes costs O(N), not O(N^2). */
	while (scope != NULL && scope_is_completed(scope, with_zombies, completed_child_scope)) {
#ifdef TRUE_ASYNC_TEST_HOOKS
		async_collector_check_records_wake(&scope->event.callbacks, member);
#endif
		async_callbacks_notify((async_awaitable_t *) &scope->event, &scope->event.callbacks, NULL, NULL);
		completed_child_scope = scope;
		scope = scope->parent_scope;
	}
}

///////////////////////////////////////////////////////////////////
/// Disposal
///////////////////////////////////////////////////////////////////

/* Withdraws disposeAfterTimeout()'s timer when it is armed, as timeout.c's disarm; a completed op
 * withdraws as nothing. */
static void scope_dispose_timer_disarm(async_scope_t *scope)
{
	async_io_event_t *timer = scope->dispose_timer;

	if (EXPECTED(timer == NULL)) {
		return;
	}

	scope->dispose_timer = NULL;
	async_io_event_orphan(timer);
	async_callbacks_remove(&timer->base.callbacks, &scope->dispose_timer_callback);
	async_io_event_release(timer);
}

/* The timer can still fire: a forked child's rebuild drops the parent's ops from the reactor's lists
 * unrun (timeout.c's check). */
static bool scope_dispose_timer_is_armed(const async_scope_t *scope)
{
	return scope->dispose_timer != NULL && scope->dispose_timer->reactor_link.prev != NULL;
}

static zend_always_inline bool scope_is_finally_run(const async_scope_t *scope)
{
	return (scope->event.flags & ASYNC_SCOPE_F_FINALLY_RUN) != 0;
}

/* A finally run below the scope, which its deadline still has to reach. */
static bool scope_has_finally_run_below(const async_scope_t *scope)
{
	for (uint32_t i = 0; i < scope->child_scopes.length; i++) {
		const async_scope_t *child_scope = scope->child_scopes.data[i];

		if (scope_is_finally_run(child_scope) || scope_has_finally_run_below(child_scope)) {
			return true;
		}
	}

	return false;
}

/* The deadline's cancel of every coroutine in the finally runs below the scope, whatever their state, and
 * never safely: unlike a scope cancel it reaches a run, a handler that caught an earlier one, and a zombie
 * (S9-scope.md 13). A closed run takes no new worker; it and the runs its workers start call no more handlers
 * (S9-scope.md 14). A cancel only queues, so no PHP code changes the vectors under the loops. */
static void scope_deadline_cancel_finally_runs(async_scope_t *scope, zend_object *error, const bool is_under_run)
{
	const bool is_in_run = is_under_run || scope_is_finally_run(scope);

	if (UNEXPECTED(is_in_run)) {
		if (scope_is_finally_run(scope)) {
			scope->event.flags |= ASYNC_SCOPE_F_CLOSED | ASYNC_SCOPE_F_DEADLINE_PASSED;
		}

		for (uint32_t i = 0; i < scope->coroutines.length; i++) {
			async_coroutine_cancel(scope->coroutines.data[i], error, false, false);
		}
	}

	for (uint32_t i = 0; i < scope->child_scopes.length; i++) {
		scope_deadline_cancel_finally_runs(scope->child_scopes.data[i], error, is_in_run);
	}
}

void async_scope_finally_run_end(async_scope_t *scope)
{
	scope->event.flags &= ~ASYNC_SCOPE_F_FINALLY_RUN;
}

bool async_scope_is_past_deadline(const async_scope_t *scope)
{
	for (; scope != NULL; scope = scope->parent_scope) {
		if (UNEXPECTED((scope->event.flags & ASYNC_SCOPE_F_DEADLINE_PASSED) != 0)) {
			return true;
		}
	}

	return false;
}

/* Moves the references `handler` holds into `released_values`, made on the first one; the copy of a
 * __call trampoline goes here, which runs no PHP code. */
static void scope_handler_keep_back(zend_fcall_info_cache *handler, zend_array **released_values)
{
	if (EXPECTED(!ZEND_FCC_INITIALIZED(*handler))) {
		return;
	}

	if (*released_values == NULL) {
		*released_values = zend_new_array(4);
	}

	zval held_value;

	if (handler->object != NULL) {
		ZVAL_OBJ(&held_value, handler->object);
		zend_hash_next_index_insert_new(*released_values, &held_value);
	}

	zend_release_fcall_info_cache(handler);

	if (handler->closure != NULL) {
		ZVAL_OBJ(&held_value, handler->closure);
		zend_hash_next_index_insert_new(*released_values, &held_value);
	}

	*handler = empty_fcall_info_cache;
}

/* Releases the freed scopes' handlers and contexts and the scope objects given back to the GC, once the
 * caller reads no scope pointer any more: the release runs destructors, which may dispose other scopes,
 * the parent of a freed one included. */
static void scope_values_release(zend_array *released_values)
{
	if (UNEXPECTED(released_values != NULL)) {
		zend_array_release(released_values);
	}
}

/* Frees the scope and its child scopes, whose coroutines are all gone or detached, keeping the references
 * of their handlers and contexts back in `released_values`. The object stays with whoever holds it and
 * reads as closed; a context held elsewhere answers from its own table. */
static void scope_free(async_scope_t *scope, zend_array **released_values)
{
	/* Before the child scopes go: the object's get_gc walks them. */
	if (scope->scope_object != NULL) {
		async_scope_object_from_object(scope->scope_object)->scope = NULL;
	}

	for (uint32_t i = 0; i < scope->child_scopes.length; i++) {
		scope_free(scope->child_scopes.data[i], released_values);
	}

	for (uint32_t i = 0; i < scope->coroutines.length; i++) {
		scope->coroutines.data[i]->scope = NULL;
	}

	scope_dispose_timer_disarm(scope);
	async_callbacks_free((async_awaitable_t *) &scope->event, &scope->event.callbacks);

	if (scope->child_scopes.data != NULL) {
		efree(scope->child_scopes.data);
	}

	if (scope->coroutines.data != NULL) {
		efree(scope->coroutines.data);
	}

	scope_handler_keep_back(&scope->exception_handler, released_values);
	scope_handler_keep_back(&scope->child_exception_handler, released_values);

	/* Handlers no disposal started, or whose start was refused: released unrun. */
	if (UNEXPECTED(scope->finally_handlers != NULL)) {
		zval finally_handlers;

		if (*released_values == NULL) {
			*released_values = zend_new_array(1);
		}

		ZVAL_ARR(&finally_handlers, scope->finally_handlers);
		zend_hash_next_index_insert_new(*released_values, &finally_handlers);
		scope->finally_handlers = NULL;
	}

	if (scope->context != NULL) {
		zval context;

		if (*released_values == NULL) {
			*released_values = zend_new_array(1);
		}

		async_context_detach_scope(scope->context);
		ZVAL_OBJ(&context, scope->context);
		zend_hash_next_index_insert_new(*released_values, &context);
		scope->context = NULL;
	}

	if (scope->filename != NULL) {
		zend_string_release_ex(scope->filename, false);
	}

	efree(scope);
}

/* The scope and each parent that only its object reaches now, after a coroutine left or a child scope went,
 * report their user values again (scope_object_get_gc): the objects are kept in `released_values`, whose
 * release puts them back in the GC's root buffer, which dropped them as live before. Not released here: a
 * collection the release starts may free the scopes the caller reads next. */
static void scope_objects_give_back_to_gc(const async_scope_t *scope, zend_array **released_values)
{
	const async_scope_t *disposable_child_scope = NULL;

	while (scope != NULL && scope_is_reached_only_by_object(scope, disposable_child_scope)) {
		zend_object *scope_object = scope->scope_object;

		if (scope_object != NULL && scope_has_user_values(scope) && GC_INFO(scope_object) == 0) {
			zval held_value;

			if (*released_values == NULL) {
				*released_values = zend_new_array(1);
			}

			ZVAL_OBJ_COPY(&held_value, scope_object);
			zend_hash_next_index_insert_new(*released_values, &held_value);
		}

		/* The object keeps the scope from being disposed, so it reaches the parent's values too. */
		if (scope_object != NULL && !(scope->event.flags & ASYNC_SCOPE_F_CANCELLED)) {
			break;
		}

		disposable_child_scope = scope;
		scope = scope->parent_scope;
	}
}

/* Starts the finally handlers of the child scopes, then the scope's own, as TrueAsync's disposal
 * disposes the child scopes first (scope.c:1203-1236). True when a run started: its scope keeps this
 * one, whose disposal comes again once the run's scope goes. Backwards: a refused start may dispose a
 * child scope, whose last sibling then takes its place. Every scope on the walk is DISPOSING, so the
 * disposal a refused start passes up stops below the walk instead of freeing a scope it is in. */
static bool scope_finally_start(async_scope_t *scope)
{
	bool is_started = false;

	scope->event.flags |= ASYNC_SCOPE_F_DISPOSING;

	for (uint32_t i = scope->child_scopes.length; i-- > 0;) {
		is_started |= scope_finally_start(scope->child_scopes.data[i]);
	}

	HashTable *finally_handlers = scope->finally_handlers;

	if (UNEXPECTED(finally_handlers != NULL)) {
		scope->finally_handlers = NULL;

		if (async_finally_handlers_start(finally_handlers, scope, scope->scope_object)) {
			is_started = true;
		} else {
			/* Released unrun with the scope, after the walk. */
			scope->finally_handlers = finally_handlers;
		}
	}

	scope->event.flags &= ~ASYNC_SCOPE_F_DISPOSING;

	return is_started;
}

/* TrueAsync's scope_dispose (scope.c:1179-1300) for a scope that can be disposed: its finally handlers
 * start, and it stays until they end; else it leaves its parent, which the last child to go disposes in
 * turn when it can, and goes with its child scopes. */
static void scope_dispose(async_scope_t *scope, zend_array **released_values)
{
	if (UNEXPECTED(scope->event.flags & ASYNC_SCOPE_F_DISPOSING)) {
		return;
	}

	if (scope_finally_start(scope)) {
		return;
	}

	async_scope_t *parent_scope = scope->parent_scope;

	if (EXPECTED(parent_scope != NULL)) {
		scope_remove_child(parent_scope, scope);
	}

	scope_free(scope, released_values);

	if (parent_scope == NULL) {
		return;
	}

	if (parent_scope->child_scopes.length == 0 && scope_can_be_disposed(parent_scope)) {
		scope_dispose(parent_scope, released_values);
		return;
	}

	scope_objects_give_back_to_gc(parent_scope, released_values);
}

static void scope_remove_coroutine(async_coroutine_t *coroutine, zend_array **released_values)
{
	async_scope_t *scope = coroutine->scope;

	scope_detach_coroutine(coroutine);
	scope_notify_completion(scope, true, coroutine);

	/* scope_dispose gives the parent back itself. */
	if (UNEXPECTED(scope_can_be_disposed(scope))) {
		scope_dispose(scope, released_values);
	} else {
		scope_objects_give_back_to_gc(scope, released_values);
	}
}

void async_scope_remove_coroutine(async_coroutine_t *coroutine)
{
	zend_array *released_values = NULL;

	scope_remove_coroutine(coroutine, &released_values);
	scope_values_release(released_values);
}

///////////////////////////////////////////////////////////////////
/// Cancellation
///////////////////////////////////////////////////////////////////

/* The cancel or dispose of a scope that completed or was cancelled before notifies nothing, as
 * TrueAsync's (scope.c:964-971), and closes the channels bound to it (S9-channel.md 5). The walk keeps a
 * notify's protocol without waking the other subscribers, an awaitAfterCancellation() waiter among them:
 * a record that a close's wake removes moves no subscriber past the cursor, and the closes run in
 * scheduler context. */
static void scope_close_bound_channels(async_scope_t *scope)
{
	async_callbacks_vector_t *const vector = &scope->event.callbacks;

	/* No subscriber of a scope's event cancels a scope; as in async_callbacks_notify(), a notify running on
	 * the vector would keep its cursor. */
	ZEND_ASSERT(!(vector->capacity & ASYNC_CALLBACKS_F_NOTIFYING));

	if (EXPECTED(vector->length == 0) || UNEXPECTED(vector->capacity & ASYNC_CALLBACKS_F_NOTIFYING)) {
		return;
	}

	vector->capacity |= ASYNC_CALLBACKS_F_NOTIFYING;
	vector->cursor = 0;

	const bool was_in_scheduler_context = ZEND_ASYNC_IN_SCHEDULER_CONTEXT;
	ZEND_ASYNC_IN_SCHEDULER_CONTEXT = true;

	while (vector->cursor < vector->length) {
		async_event_callback_t *const subscriber = async_callbacks_slots(vector)[vector->cursor++];

		if (async_channel_is_owner_scope_subscriber(subscriber)) {
			async_channel_close_for_owner_scope(subscriber);
		}
	}

	vector->capacity &= ~ASYNC_CALLBACKS_F_NOTIFYING;
	ZEND_ASYNC_IN_SCHEDULER_CONTEXT = was_in_scheduler_context;
}

void async_scope_cancel(async_scope_t *scope, zend_object *error, bool transfer_error, const bool is_safely)
{
	/* A closed scope has nothing left to cancel; a finally run's scope, which a stand-in object can reach,
	 * is out of every scope cancel's reach, and only an ancestor's deadline stops it from outside. */
	if (UNEXPECTED(scope->event.flags & (ASYNC_SCOPE_F_CLOSED | ASYNC_SCOPE_F_FINALLY_RUN))) {
		if (error != NULL && transfer_error) {
			OBJ_RELEASE(error);
		}

		return;
	}

	if (scope_is_completed(scope, true, NULL)) {
		scope->event.flags |= ASYNC_SCOPE_F_CLOSED;
		scope_close_bound_channels(scope);

		/* The cancel reaches the subtree, as the TrueAsync docs say (concepts/scope.md), though TrueAsync's
		 * scope.c:964-971 stops at this scope. A child scope with a coroutine of its own is skipped: a
		 * cancelled one is left to unwind. A finally run's scope returns at the cancel's first check. A
		 * cancel only queues, so no PHP code changes the vector under the loop. */
		for (uint32_t i = 0; i < scope->child_scopes.length; i++) {
			async_scope_t *const child_scope = scope->child_scopes.data[i];

			if (EXPECTED(child_scope->coroutines.length == 0)) {
				async_scope_cancel(child_scope, error, false, is_safely);
			}
		}

		if (error != NULL && transfer_error) {
			OBJ_RELEASE(error);
		}

		/* As TrueAsync's (scope.c:964-971); the run's scope keeps this one. A refused start leaves the handlers
		 * to the scope's disposal, which tries again: released here, they would run destructors inside the
		 * cascade of the cancel walking this scope's parent. */
		HashTable *finally_handlers = scope->finally_handlers;

		if (UNEXPECTED(finally_handlers != NULL)) {
			scope->finally_handlers = NULL;

			if (UNEXPECTED(!async_finally_handlers_start(finally_handlers, scope, scope->scope_object))) {
				scope->finally_handlers = finally_handlers;
			}
		}

		/* After the starts above, which add the runs: the timer's fire still has to stop a finally run below;
		 * with none, it would find the scope closed and do nothing. */
		if (UNEXPECTED(scope->dispose_timer != NULL) && !scope_has_finally_run_below(scope)) {
			scope_dispose_timer_disarm(scope);
		}

		return;
	}

	if (error == NULL) {
		error = async_new_exception(async_ce_cancellation, "Scope was cancelled");
		transfer_error = true;
	}

	scope_set_cancelled(scope);

	/* A cancel only queues and a closed child scope's finally run starts in a worker, or is left to its
	 * disposal when refused, so no PHP code changes either vector under the loops. */
	for (uint32_t i = 0; i < scope->child_scopes.length; i++) {
		async_scope_t *const child_scope = scope->child_scopes.data[i];

		if (EXPECTED(!scope_is_finally_run(child_scope))) {
			async_scope_cancel(child_scope, error, false, is_safely);
		}
	}

	for (uint32_t i = 0; i < scope->coroutines.length; i++) {
		async_coroutine_cancel(scope->coroutines.data[i], error, false, is_safely);
	}

#ifdef TRUE_ASYNC_TEST_HOOKS
	async_collector_check_records_wake(&scope->event.callbacks, NULL);
#endif
	async_callbacks_notify((async_awaitable_t *) &scope->event, &scope->event.callbacks, NULL, error);

	if (transfer_error) {
		OBJ_RELEASE(error);
	}
}

///////////////////////////////////////////////////////////////////
/// The error route
///////////////////////////////////////////////////////////////////

/* A scope without an object (the global scope, or one whose object is gone) gets a stand-in for the
 * hooks of a SpawnStrategy and for its error handler, its object while anything holds it (S9-scope.md
 * 4 and 9, item 8). */
static zend_object *scope_stand_in_attach(async_scope_t *scope)
{
	async_scope_object_t *stand_in = zend_object_alloc(sizeof(async_scope_object_t), async_ce_scope);

	zend_object_std_init(&stand_in->std, async_ce_scope);
	object_properties_init(&stand_in->std, async_ce_scope);
	stand_in->scope = scope;
	stand_in->is_cancelled = (scope->event.flags & ASYNC_SCOPE_F_CANCELLED) != 0;
	stand_in->is_stand_in = true;
	scope->scope_object = &stand_in->std;

	return &stand_in->std;
}

/* Calls the handler `scope` has for `*error`: the child scope handler when the error comes from a child
 * scope and that handler is set, else the scope's own, as fn(Scope $scope, Coroutine $coroutine,
 * Throwable $error) (TrueAsync's try_to_handle_exception, scope.c:1594-1718, where a child scope handler
 * that throws leaves the own one uncalled). True when it returned without throwing, or called exit(),
 * which ends the request as a coroutine's exit() does (D16). A handler's exception replaces `*error`,
 * which becomes its previous. The coroutine is finished and still current, so the handler cannot park:
 * suspend() and await() throw there. */
static bool scope_handle_error(async_scope_t *scope,
							   const bool is_from_child_scope,
							   async_coroutine_t *coroutine,
							   zend_object **error)
{
	const zend_fcall_info_cache *handler = NULL;

	if (is_from_child_scope && ZEND_FCC_INITIALIZED(scope->child_exception_handler)) {
		handler = &scope->child_exception_handler;
	} else if (ZEND_FCC_INITIALIZED(scope->exception_handler)) {
		handler = &scope->exception_handler;
	} else {
		return false;
	}

	zend_object *scope_object = scope->scope_object;

	if (EXPECTED(scope_object != NULL)) {
		GC_ADDREF(scope_object);
	} else {
		scope_object = scope_stand_in_attach(scope);
	}

	zval arguments[3];
	zval retval;

	ZVAL_OBJ(&arguments[0], scope_object);
	ZVAL_OBJ(&arguments[1], &coroutine->std);
	ZVAL_OBJ(&arguments[2], *error);
	/* The call holds the closure and $this, so a handler may replace itself. */
	zend_call_known_fcc(handler, &retval, 3, arguments, NULL);
	zval_ptr_dtor(&retval);

	zend_object *handler_exception = EG(exception);

	if (UNEXPECTED(handler_exception != NULL)) {
		GC_ADDREF(handler_exception);
		zend_clear_exception();
	}

	OBJ_RELEASE(scope_object);

	if (EXPECTED(handler_exception == NULL)) {
		return true;
	}

	if (UNEXPECTED(zend_is_unwind_exit(handler_exception) || zend_is_graceful_exit(handler_exception))) {
		const bool is_exit = zend_is_unwind_exit(handler_exception);
		OBJ_RELEASE(handler_exception);

		if (is_exit) {
			async_scheduler_cancel_for_exit();
		}

		return true;
	}

	/* A handler that rethrows the error passes it on unchanged. */
	if (handler_exception == *error) {
		OBJ_RELEASE(handler_exception);
		return false;
	}

	zend_exception_set_previous(handler_exception, *error);
	*error = handler_exception;

	return false;
}

#ifdef TRUE_ASYNC_TEST_HOOKS
/* The collector leaves out the route, whose level gets its scope's object, and a SpawnStrategy's hooks
 * for a null provideScope(), which get the current scope's (S7.md 10): what it found in that subtree
 * is handed out, as registry_cancel() hands out what it cancels, so the oracle excuses them. Marked
 * once, before the calls: a handler cannot park, and a run that starts while a hook parks finds
 * nothing there, as the hook's caller holds the scope's object. */
static void scope_hand_out_found(async_scope_t *scope)
{
	for (uint32_t i = 0; i < scope->coroutines.length; i++) {
		zend_coroutine_t *member = &scope->coroutines.data[i]->coroutine;

		if (UNEXPECTED(member->flags & ASYNC_COROUTINE_F_DEADLOCK_FOUND)) {
			member->flags |= ASYNC_COROUTINE_F_HANDED_OUT;
		}
	}

	/* The waiters in awaitCompletion() and on the scope's channels, wherever they run: the cancels that
	 * follow wake them. */
	async_event_callback_t **callback_slots = async_callbacks_slots(&scope->event.callbacks);

	for (uint32_t i = 0; i < scope->event.callbacks.length; i++) {
		if (EXPECTED(callback_slots[i]->flags & ASYNC_CALLBACK_F_RECORD)) {
			zend_coroutine_t *waiter = &((async_coroutine_event_callback_t *) callback_slots[i])->coroutine->coroutine;

			if (UNEXPECTED(waiter->flags & ASYNC_COROUTINE_F_DEADLOCK_FOUND)) {
				waiter->flags |= ASYNC_COROUTINE_F_HANDED_OUT;
			}
		} else if (async_channel_is_owner_scope_subscriber(callback_slots[i])) {
			async_channel_hand_out_found(callback_slots[i]);
		}
	}

	for (uint32_t i = 0; i < scope->child_scopes.length; i++) {
		scope_hand_out_found(scope->child_scopes.data[i]);
	}
}
#endif

bool async_scope_catch(async_coroutine_t *coroutine, zend_object *error)
{
	async_scope_t *scope = coroutine->scope;
	/* From the coroutine's own scope, for every scope the route reaches (TrueAsync's coroutine.c:719). */
	const bool is_safely = (scope->event.flags & ASYNC_SCOPE_F_DISPOSE_SAFELY) != 0;
	bool is_from_child_scope = false;
	bool is_handled = false;

	/* The route's own reference: a handler's exception replaces the error on the way up. */
	GC_ADDREF(error);

	/* Every scope on the way keeps a member, the coroutine or a child scope, until the coroutine leaves
	 * its scope after the route, so none is disposed under the loop. An exception the cancels leave (a
	 * refused finally start's stack error) stops the route; the caller ends the request with it. */
	while (scope != NULL && EXPECTED(EG(exception) == NULL)) {
		bool is_taken = false;

#ifdef TRUE_ASYNC_TEST_HOOKS
		scope_hand_out_found(scope);
#endif

		/* A fatal error in a handler skips the caller's removal from the scope; the coroutine leaves it
		 * here, and the request's end frees the scope. */
		zend_try
		{
			is_taken = scope_handle_error(scope, is_from_child_scope, coroutine, &error);
		}
		zend_catch
		{
			scope_detach_coroutine(coroutine);
			zend_bailout();
		}
		zend_end_try();

		if (is_taken) {
			is_handled = true;
			break;
		}

		scope_set_cancelled(scope);

		/* Fresh cancellations, not the error (scope.c:1020-1044); the loops hold as async_scope_cancel()'s. */
		for (uint32_t i = 0; i < scope->child_scopes.length; i++) {
			async_scope_t *const child_scope = scope->child_scopes.data[i];

			if (EXPECTED(!scope_is_finally_run(child_scope))) {
				async_scope_cancel(child_scope, NULL, false, is_safely);
			}
		}

		for (uint32_t i = 0; i < scope->coroutines.length; i++) {
			async_coroutine_cancel(scope->coroutines.data[i], NULL, false, is_safely);
		}

		/* A waiter in awaitCompletion() takes the error (TrueAsync's resolve callback marks the scope's
		 * event handled, scope.c:1046-1062). */
		if (async_callbacks_notify((async_awaitable_t *) &scope->event, &scope->event.callbacks, NULL, error)) {
			is_handled = true;
			break;
		}

		is_from_child_scope = true;
		scope = scope->parent_scope;
	}

	OBJ_RELEASE(error);

	return is_handled;
}

/* An ancestor's dispose timer, whose fire reaches the scope's finally run. */
static bool scope_has_deadline_above(const async_scope_t *scope)
{
	for (const async_scope_t *ancestor = scope->parent_scope; ancestor != NULL; ancestor = ancestor->parent_scope) {
		if (scope_dispose_timer_is_armed(ancestor)) {
			return true;
		}
	}

	return false;
}

/* The reach of `scope` and its parents, with an edge from the scope's node to `node`. */
static void scope_collector_reach_from(async_collector_t *collector, async_scope_t *scope, const uint32_t node)
{
	uint32_t reached_node = node;

	for (; scope != NULL; scope = scope->parent_scope) {
		bool is_new_node;
		const uint32_t scope_node = async_collector_reach_node(collector, &scope->coroutines, &is_new_node);

		async_collector_report_reach(collector, scope_node, reached_node);

		/* Its parents are linked already. */
		if (EXPECTED(!is_new_node)) {
			return;
		}

		if (scope->scope_object != NULL) {
			async_collector_report_holder(collector, scope->scope_object, scope_node);
		}

		/* It cancels the scope when it throws, holding no object. */
		if (UNEXPECTED(scope->iterator_coroutine != NULL)) {
			async_collector_report_holder(collector, &scope->iterator_coroutine->std, scope_node);
		}

		/* It cancels the scope when it fires, holding no object; a cancelled or closed scope's fire does not
		 * reach its own coroutines. */
		if (UNEXPECTED(scope_dispose_timer_is_armed(scope) &&
					   !(scope->event.flags & (ASYNC_SCOPE_F_CANCELLED | ASYNC_SCOPE_F_CLOSED)))) {
			async_collector_report_live_reach(collector, scope_node);
		}

		/* An ancestor's deadline stops the run when it fires; no scope cancel reaches it. */
		if (UNEXPECTED(scope_is_finally_run(scope) && scope_has_deadline_above(scope))) {
			async_collector_report_live_reach(collector, scope_node);
		}

		reached_node = scope_node;
	}
}

void async_scope_collector_reach(async_collector_t *collector, async_coroutine_t *coroutine, const uint32_t node)
{
	scope_collector_reach_from(collector, coroutine->scope, node);
}

///////////////////////////////////////////////////////////////////
/// Spawn
///////////////////////////////////////////////////////////////////

void async_scope_discard_coroutine(async_coroutine_t *coroutine)
{
	async_scope_remove_coroutine(coroutine);
	zend_hash_index_del(&ASYNC_G(coroutines), coroutine->std.handle);
	OBJ_RELEASE(&coroutine->std);
}

static bool spawn_strategy_call(zend_object *spawn_strategy,
								const char *method_name,
								const size_t method_name_length,
								async_coroutine_t *coroutine,
								zend_object *scope_object)
{
	zval coroutine_value, scope_value, retval;

	ZVAL_OBJ(&coroutine_value, &coroutine->std);
	ZVAL_OBJ(&scope_value, scope_object);
	zend_call_method(spawn_strategy,
					 spawn_strategy->ce,
					 NULL,
					 method_name,
					 method_name_length,
					 &retval,
					 2,
					 &coroutine_value,
					 &scope_value);
	zval_ptr_dtor(&retval);

	return EG(exception) == NULL;
}

/* A hook threw: the coroutine ends cancelled, so whoever kept it sees it end, and the hook's exception
 * stays the pending one. */
static void spawn_cancel(async_coroutine_t *coroutine)
{
	zend_object *hook_exception = EG(exception);

	EG(exception) = NULL;

	/* The cancel's enqueue fails only without a stack for the scheduler; the coroutine then never ran. */
	if (UNEXPECTED(!async_coroutine_cancel(coroutine, NULL, false, false))) {
		zend_clear_exception();

		if (ZEND_COROUTINE_STATUS(&coroutine->coroutine) == ZEND_COROUTINE_STATUS_CREATED) {
			async_scope_discard_coroutine(coroutine);
		}
	}

	EG(exception) = hook_exception;
}

/* The hooks may suspend: the coroutine may finish meanwhile and take its scope with it, so this frame
 * holds the coroutine and does not touch the scope after the first hook. */
static async_coroutine_t *
spawn_with_strategy(async_scope_t *scope, zend_object *spawn_strategy, async_coroutine_t *coroutine)
{
	zend_object *scope_object = scope->scope_object;

	if (EXPECTED(scope_object != NULL)) {
		GC_ADDREF(scope_object);
	} else {
		scope_object = scope_stand_in_attach(scope);
	}

	GC_ADDREF(&coroutine->std);

	bool succeeded = spawn_strategy_call(spawn_strategy, ZEND_STRL("beforeCoroutineEnqueue"), coroutine, scope_object);

	if (EXPECTED(succeeded && !ZEND_COROUTINE_IS_FINISHED(&coroutine->coroutine))) {
		/* Refused only when the scheduler coroutine cannot get a stack, so the coroutine is still CREATED,
		 * or QUEUED by a hook, which the enqueue accepts. */
		if (UNEXPECTED(!async_scheduler_enqueue(&coroutine->coroutine, NULL, false))) {
			async_scope_discard_coroutine(coroutine);
			OBJ_RELEASE(&coroutine->std);
			coroutine = NULL;
		} else {
			succeeded =
					spawn_strategy_call(spawn_strategy, ZEND_STRL("afterCoroutineEnqueue"), coroutine, scope_object);
		}
	}

	OBJ_RELEASE(scope_object);

	if (UNEXPECTED(coroutine == NULL)) {
		return NULL;
	}

	if (UNEXPECTED(!succeeded)) {
		spawn_cancel(coroutine);
		OBJ_RELEASE(&coroutine->std);
		return NULL;
	}

	return coroutine;
}

async_coroutine_t *async_scope_spawn(async_scope_t *scope,
									 zend_object *spawn_strategy,
									 zend_fcall_info *fci,
									 zend_fcall_info_cache *fcc,
									 zval *args,
									 const uint32_t args_count,
									 HashTable *named_args)
{
	if (UNEXPECTED(scope->event.flags & ASYNC_SCOPE_F_CLOSED)) {
		zend_release_fcall_info_cache(fcc);
		zend_throw_exception(async_ce_async_exception, "Cannot spawn a coroutine in a closed scope", 0);
		return NULL;
	}

	ASYNC_IO_PROVIDER_INSTALL_ONCE();

	async_coroutine_t *coroutine = async_coroutine_new();

	/* ZEND_ASYNC_FCALL_DEFINE into the coroutine's own block. */
	zend_fcall_t *fcall = &coroutine->spawn_fcall;
	fcall->fci = *fci;
	fcall->fci_cache = *fcc;

	if (args_count != 0) {
		fcall->fci.param_count = args_count;
		fcall->fci.params = safe_emalloc(args_count, sizeof(zval), 0);

		for (uint32_t i = 0; i < args_count; i++) {
			ZVAL_COPY(&fcall->fci.params[i], &args[i]);
		}
	}

	if (UNEXPECTED(named_args != NULL)) {
		fcall->fci.named_params = named_args;
		GC_ADDREF(named_args);
	}

	Z_TRY_ADDREF(fcall->fci.function_name);

	/* The call comes after this frame, and the callable's name alone may not resolve again: the cache
	 * keeps the object a class-string callable resolved to ($this of the spawning method) and a __call
	 * trampoline, which the call consumes. */
	zend_fcc_addref(&fcall->fci_cache);
	coroutine->coroutine.fcall = fcall;

	zend_string *filename = zend_get_executed_filename_ex();

	coroutine->coroutine.filename = filename != NULL ? zend_string_copy(filename) : NULL;
	coroutine->coroutine.lineno = zend_get_executed_lineno();

	async_scope_add_coroutine(scope, coroutine);

	if (UNEXPECTED(spawn_strategy != NULL)) {
		return spawn_with_strategy(scope, spawn_strategy, coroutine);
	}

	/* A CREATED coroutine is refused only when the scheduler coroutine cannot get a stack: the coroutine
	 * then never existed. */
	if (UNEXPECTED(!async_scheduler_enqueue(&coroutine->coroutine, NULL, false))) {
		async_scope_discard_coroutine(coroutine);
		return NULL;
	}

	GC_ADDREF(&coroutine->std);

	return coroutine;
}

///////////////////////////////////////////////////////////////////
/// The object
///////////////////////////////////////////////////////////////////

async_scope_t *async_scope_new(async_scope_t *parent_scope)
{
	async_scope_t *scope = ecalloc(1, sizeof(async_scope_t));
	zend_string *filename = zend_get_executed_filename_ex();

	async_event_init(&scope->event, 0);
	scope->filename = filename != NULL ? zend_string_copy(filename) : NULL;
	scope->lineno = zend_get_executed_lineno();

	if (parent_scope != NULL) {
		scope->event.flags |= parent_scope->event.flags & ASYNC_SCOPE_F_DISPOSE_SAFELY;
		scope_add_child(parent_scope, scope);
	}

	return scope;
}

static zend_object *scope_object_new(zend_class_entry *class_entry, async_scope_t *parent_scope)
{
	async_scope_object_t *scope_object = zend_object_alloc(sizeof(async_scope_object_t), class_entry);

	zend_object_std_init(&scope_object->std, class_entry);
	object_properties_init(&scope_object->std, class_entry);

	scope_object->scope = async_scope_new(parent_scope);
	scope_object->scope->scope_object = &scope_object->std;
	scope_object->is_cancelled = false;
	scope_object->is_stand_in = false;

	return &scope_object->std;
}

/* `new Scope()`: a root with unsafe disposal (TrueAsync's async_new_scope, scope.c:1302-1381). */
static zend_object *scope_object_create(zend_class_entry *class_entry)
{
	return scope_object_new(class_entry, NULL);
}

/* The object goes, the scope stays until its coroutines finish: disposed now when nothing keeps it,
 * else cancelled when `may_cancel` (TrueAsync's scope_destroy, scope.c:1395-1416). */
static void scope_object_release_scope(async_scope_object_t *scope_object, const bool may_cancel)
{
	async_scope_t *scope = scope_object->scope;

	scope_object->scope = NULL;
	scope->scope_object = NULL;

	if (scope_can_be_disposed(scope)) {
		zend_array *released_values = NULL;
		scope_dispose(scope, &released_values);
		scope_values_release(released_values);
		return;
	}

	if (may_cancel) {
		zend_object *error =
				async_new_exception(async_ce_cancellation, "Scope is being disposed due to object destruction");

		async_scope_cancel(scope, error, true, (scope->event.flags & ASYNC_SCOPE_F_DISPOSE_SAFELY) != 0);
	}
}

/* The handlers, finally handlers included, and the context, only while nothing but the object reaches the
 * scope: otherwise a cycle through one of them would let the GC call the destructor, which closes or
 * cancels a scope still in use. The destructor detaches the scope before the GC frees such a cycle. */
static HashTable *scope_object_get_gc(zend_object *object, zval **table, int *num)
{
	async_scope_t *scope = async_scope_object_from_object(object)->scope;
	zend_get_gc_buffer *gc_buffer = zend_get_gc_buffer_create();

	if (EXPECTED(scope != NULL) && scope_has_user_values(scope) && scope_is_reached_only_by_object(scope, NULL)) {
		if (UNEXPECTED(ZEND_FCC_INITIALIZED(scope->exception_handler))) {
			zend_get_gc_buffer_add_fcc(gc_buffer, &scope->exception_handler);
		}

		if (UNEXPECTED(ZEND_FCC_INITIALIZED(scope->child_exception_handler))) {
			zend_get_gc_buffer_add_fcc(gc_buffer, &scope->child_exception_handler);
		}

		if (UNEXPECTED(scope->finally_handlers != NULL)) {
			zend_get_gc_buffer_add_ht(gc_buffer, scope->finally_handlers);
		}

		if (UNEXPECTED(scope->context != NULL)) {
			zend_get_gc_buffer_add_obj(gc_buffer, scope->context);
		}
	}

	zend_get_gc_buffer_use(gc_buffer, table, num);

	return NULL;
}

static void scope_object_destroy(zend_object *object)
{
	async_scope_object_t *scope_object = async_scope_object_from_object(object);

	if (scope_object->scope != NULL) {
		scope_object_release_scope(scope_object, !scope_object->is_stand_in);
	}
}

/* The destructor did not run: the constructor failed, or a bailout skipped the shutdown's destructors.
 * Once the request's scheduler is gone (the core turned async off) nothing is cancelled: a scope the
 * object cannot dispose stays to the end of the request's memory. */
static void scope_object_free(zend_object *object)
{
	async_scope_object_t *scope_object = async_scope_object_from_object(object);

	if (scope_object->scope != NULL) {
		scope_object_release_scope(scope_object, ZEND_ASYNC_IS_ACTIVE && !scope_object->is_stand_in);
	}

	zend_object_std_dtor(object);
}

///////////////////////////////////////////////////////////////////
/// Methods
///////////////////////////////////////////////////////////////////

#define THIS_SCOPE_OBJECT (async_scope_object_from_object(Z_OBJ_P(ZEND_THIS)))

/* The scope of $this, or NULL with an exception once it is gone. */
static async_scope_t *this_scope(zval *this_value)
{
	async_scope_t *scope = async_scope_object_from_object(Z_OBJ_P(this_value))->scope;

	if (UNEXPECTED(scope == NULL)) {
		zend_throw_exception(async_ce_async_exception, "Scope object has been disposed", 0);
	}

	return scope;
}

ZEND_METHOD(Async_Scope, inherit)
{
	zend_object *parent_object = NULL;

	ZEND_PARSE_PARAMETERS_START(0, 1)
		Z_PARAM_OPTIONAL
		Z_PARAM_OBJ_OF_CLASS_OR_NULL(parent_object, async_ce_scope)
	ZEND_PARSE_PARAMETERS_END();

	async_scope_t *parent_scope = async_scope_current();

	if (parent_object != NULL) {
		parent_scope = async_scope_object_from_object(parent_object)->scope;

		if (UNEXPECTED(parent_scope == NULL)) {
			zend_throw_exception(async_ce_async_exception,
								 "Cannot inherit a Scope from a parent Scope that has already been disposed.",
								 0);
			RETURN_THROWS();
		}
	}

	RETURN_OBJ(scope_object_new(async_ce_scope, parent_scope));
}

ZEND_METHOD(Async_Scope, provideScope)
{
	ZEND_PARSE_PARAMETERS_NONE();

	RETURN_OBJ_COPY(Z_OBJ_P(ZEND_THIS));
}

ZEND_METHOD(Async_Scope, __construct)
{
	ZEND_PARSE_PARAMETERS_NONE();
}

ZEND_METHOD(Async_Scope, asNotSafely)
{
	ZEND_PARSE_PARAMETERS_NONE();

	async_scope_t *scope = this_scope(ZEND_THIS);

	if (UNEXPECTED(scope == NULL)) {
		RETURN_THROWS();
	}

	scope->event.flags &= ~ASYNC_SCOPE_F_DISPOSE_SAFELY;

	RETURN_OBJ_COPY(Z_OBJ_P(ZEND_THIS));
}

ZEND_METHOD(Async_Scope, allowZombies)
{
	ZEND_PARSE_PARAMETERS_NONE();

	async_scope_t *scope = this_scope(ZEND_THIS);

	if (UNEXPECTED(scope == NULL)) {
		RETURN_THROWS();
	}

	scope->event.flags |= ASYNC_SCOPE_F_DISPOSE_SAFELY;

	RETURN_OBJ_COPY(Z_OBJ_P(ZEND_THIS));
}

ZEND_METHOD(Async_Scope, spawn)
{
	zend_fcall_info fci;
	zend_fcall_info_cache fcc;
	zval *args = NULL;
	uint32_t args_count = 0;
	HashTable *named_args = NULL;

	THROW_IF_UNAVAILABLE();

	ZEND_PARSE_PARAMETERS_START(1, -1)
		Z_PARAM_FUNC_NO_TRAMPOLINE_FREE(fci, fcc)
		Z_PARAM_VARIADIC_WITH_NAMED(args, args_count, named_args)
	ZEND_PARSE_PARAMETERS_END();

	async_scope_t *scope = this_scope(ZEND_THIS);

	if (UNEXPECTED(scope == NULL)) {
		zend_release_fcall_info_cache(&fcc);
		RETURN_THROWS();
	}

	async_coroutine_t *coroutine = async_scope_spawn(scope, NULL, &fci, &fcc, args, args_count, named_args);

	if (UNEXPECTED(coroutine == NULL)) {
		RETURN_THROWS();
	}

	RETURN_OBJ(&coroutine->std);
}

ZEND_METHOD(Async_Scope, cancel)
{
	zend_object *cancellation = NULL;

	ZEND_PARSE_PARAMETERS_START(0, 1)
		Z_PARAM_OPTIONAL
		Z_PARAM_OBJ_OF_CLASS_OR_NULL(cancellation, async_ce_cancellation)
	ZEND_PARSE_PARAMETERS_END();

	async_scope_t *scope = THIS_SCOPE_OBJECT->scope;

	if (scope != NULL) {
		async_scope_cancel(scope, cancellation, false, (scope->event.flags & ASYNC_SCOPE_F_DISPOSE_SAFELY) != 0);
	}
}

ZEND_METHOD(Async_Scope, dispose)
{
	ZEND_PARSE_PARAMETERS_NONE();

	async_scope_t *scope = THIS_SCOPE_OBJECT->scope;

	if (scope != NULL) {
		async_scope_cancel(scope, NULL, false, (scope->event.flags & ASYNC_SCOPE_F_DISPOSE_SAFELY) != 0);
	}
}

ZEND_METHOD(Async_Scope, disposeSafely)
{
	ZEND_PARSE_PARAMETERS_NONE();

	async_scope_t *scope = THIS_SCOPE_OBJECT->scope;

	if (scope != NULL) {
		async_scope_cancel(scope, NULL, false, true);
	}
}

/* Whether async_scope_cancel() of the scope cancels a coroutine, following its branches: a closed scope and
 * a finally run's scope return at once; a completed one (a cancelled one counts) passes the cancel only to
 * its child scopes without coroutines of their own; any other cancels its coroutines, zombies included,
 * and passes the cancel to every child scope but a finally run's. */
static bool scope_deadline_interrupts_member(const async_scope_t *scope)
{
	if (UNEXPECTED(scope->event.flags & (ASYNC_SCOPE_F_CLOSED | ASYNC_SCOPE_F_FINALLY_RUN))) {
		return false;
	}

	const bool is_cancelling = !scope_is_completed(scope, true, NULL);

	if (EXPECTED(is_cancelling && scope->coroutines.length > 0)) {
		return true;
	}

	for (uint32_t i = 0; i < scope->child_scopes.length; i++) {
		const async_scope_t *child_scope = scope->child_scopes.data[i];

		if (EXPECTED(is_cancelling || child_scope->coroutines.length == 0) &&
			scope_deadline_interrupts_member(child_scope)) {
			return true;
		}
	}

	return false;
}

/* The timer's completion, in its notify. The cancel only queues, so it runs here; TrueAsync spawns a
 * coroutine of the global scope to make it (scope.c:676-723). */
static void scope_dispose_timer_fire(async_awaitable_t *target,
									 async_event_callback_t *callback,
									 void *result,
									 zend_object *exception)
{
	(void) target;
	(void) result;
	(void) exception;

	async_scope_t *scope = (async_scope_t *) ((char *) callback - offsetof(async_scope_t, dispose_timer_callback));

	scope_dispose_timer_disarm(scope);

	zend_object *error = async_new_exception(async_ce_cancellation, "Scope has been disposed due to timeout");

	const bool is_safely = (scope->event.flags & ASYNC_SCOPE_F_DISPOSE_SAFELY) != 0;

	/* When the cancel below interrupts a member, every finally run at or below the scope calls nothing from now
	 * on, the ones that cancel starts too. A safe scope's cancel lets its members run on as zombies, and one
	 * that reaches no member bounds none: their handlers still run. A request-lifetime scope (the global one is
	 * reachable through a stand-in) would keep the flag for the whole request. */
	if (EXPECTED(!is_safely && !(scope->event.flags & ASYNC_SCOPE_F_REQUEST_LIFETIME) &&
				 scope_deadline_interrupts_member(scope))) {
		scope->event.flags |= ASYNC_SCOPE_F_DEADLINE_PASSED;
	}

	/* Before the scope's cancel: on a scope left unflagged, the runs that cancel starts, for the scope itself
	 * and for its idle child scopes, call their handlers, and the walk would stop them. */
	scope_deadline_cancel_finally_runs(scope, error, false);
	async_scope_cancel(scope, error, true, is_safely);
}

/* The Timer op is on the reactor's waits, so a script that ends by itself waits for it, as TrueAsync's
 * libuv timer holds its loop. */
ZEND_METHOD(Async_Scope, disposeAfterTimeout)
{
	zend_long timeout;

	ZEND_PARSE_PARAMETERS_START(1, 1)
		Z_PARAM_LONG(timeout)
	ZEND_PARSE_PARAMETERS_END();

	if (UNEXPECTED(timeout < 0)) {
		zend_argument_value_error(1, "must be greater than or equal to 0");
		RETURN_THROWS();
	}

	async_scope_t *scope = THIS_SCOPE_OBJECT->scope;

	/* No timer fires while async is not active, and in RSHUTDOWN's final release the reactor is gone. A
	 * closed scope's timer is kept only to stop the finally runs below it; a run's own scope, reachable
	 * through a stand-in, takes none, since its fire would stop the run's handlers. */
	if (UNEXPECTED(!ZEND_ASYNC_IS_ACTIVE || scope == NULL || scope_is_finally_run(scope) ||
				   ((scope->event.flags & ASYNC_SCOPE_F_CLOSED) && !scope_has_finally_run_below(scope)) ||
				   (scope->coroutines.length == 0 && scope->child_scopes.length == 0))) {
		return;
	}

	const php_deadline deadline = timeout == 0 ? php_io_deadline_from_ns(0) : async_reactor_deadline_from_ms(timeout);

	if (scope->dispose_timer != NULL) {
		/* The rebuild runs lazily, so it goes before the check. */
		async_reactor_check_fork();

		if (scope_dispose_timer_is_armed(scope) && scope->dispose_timer->op.deadline.hrtime <= deadline.hrtime) {
			return;
		}

		scope_dispose_timer_disarm(scope);
	}

	/* timeout.c's arm; a Timer op never completes in its submit (reactor.c, queue_push). */
	async_io_event_t *timer = async_io_event_new();

	php_io_op_timer(&timer->op, deadline);
	async_callbacks_reserve(&timer->base.callbacks, 1);

	scope->dispose_timer_callback.flags = 0;
	scope->dispose_timer_callback.callback = scope_dispose_timer_fire;
	scope->dispose_timer_callback.dispose = NULL;
	async_callbacks_push_reserved(&timer->base.callbacks, &scope->dispose_timer_callback);

	scope->dispose_timer = timer;

	if (UNEXPECTED(async_io_event_submit(timer) == FAILURE)) {
		scope->dispose_timer = NULL;
		async_callbacks_remove(&timer->base.callbacks, &scope->dispose_timer_callback);
		async_io_event_release(timer);
		RETURN_THROWS();
	}
}

/* Replaces `handler` with a copy of the callable `callable`. The old one goes last: its release may run
 * a destructor. */
static void scope_handler_replace(zend_fcall_info_cache *handler, const zend_fcall_info_cache *callable)
{
	zend_fcall_info_cache old_handler = *handler;

	zend_fcc_dup(handler, callable);

	if (ZEND_FCC_INITIALIZED(old_handler)) {
		zend_fcc_dtor(&old_handler);
	}
}

ZEND_METHOD(Async_Scope, setExceptionHandler)
{
	zend_fcall_info fci;
	zend_fcall_info_cache fcc;

	ZEND_PARSE_PARAMETERS_START(1, 1)
		Z_PARAM_FUNC_NO_TRAMPOLINE_FREE(fci, fcc)
	ZEND_PARSE_PARAMETERS_END();

	async_scope_t *scope = this_scope(ZEND_THIS);

	if (UNEXPECTED(scope == NULL)) {
		zend_release_fcall_info_cache(&fcc);
		RETURN_THROWS();
	}

	scope_handler_replace(&scope->exception_handler, &fcc);
}

ZEND_METHOD(Async_Scope, setChildScopeExceptionHandler)
{
	zend_fcall_info fci;
	zend_fcall_info_cache fcc;

	ZEND_PARSE_PARAMETERS_START(1, 1)
		Z_PARAM_FUNC_NO_TRAMPOLINE_FREE(fci, fcc)
	ZEND_PARSE_PARAMETERS_END();

	async_scope_t *scope = this_scope(ZEND_THIS);

	if (UNEXPECTED(scope == NULL)) {
		zend_release_fcall_info_cache(&fcc);
		RETURN_THROWS();
	}

	scope_handler_replace(&scope->child_exception_handler, &fcc);
}

/* The record's wake: the scope completed, was cancelled or took an error on its route, or its teardown
 * fires a record left there. The enqueue unlinks the wait (D26). */
static void
scope_record_wake(async_awaitable_t *target, async_event_callback_t *callback, void *result, zend_object *exception)
{
	(void) target;
	(void) result;

	const async_coroutine_event_callback_t *record = (async_coroutine_event_callback_t *) callback;

	if (exception != NULL) {
		GC_ADDREF(exception);
	}

	async_scheduler_enqueue(&record->coroutine->coroutine, exception, true);
}

static zend_string *scope_record_info(const async_coroutine_event_callback_t *record)
{
	const async_scope_t *scope = (const async_scope_t *) record->event;

	if (UNEXPECTED(scope->filename == NULL)) {
		return zend_string_init(ZEND_STRL("await: scope"), 0);
	}

	return zend_strpprintf(0, "await: scope created at %s:%" PRIu32, ZSTR_VAL(scope->filename), scope->lineno);
}

static void
scope_report_completion_sources(const async_scope_t *scope, async_collector_t *collector, const uint32_t node);

/* An edge from `scope`'s completion node to `node`, the node's sources reported once per run. */
static void scope_report_completion_reach(const async_scope_t *scope, async_collector_t *collector, const uint32_t node)
{
	bool is_new_node;
	const uint32_t completion_node = async_collector_reach_node(collector, &scope->event, &is_new_node);

	async_collector_report_reach(collector, completion_node, node);

	if (is_new_node) {
		scope_report_completion_sources(scope, collector, completion_node);
	}
}

/* The scope's completion node, live once a coroutine of its subtree is, zombies included: any of them
 * may wake a waiter, by finishing or by an error whose route passes the scope (S9-scope.md 6). One per
 * scope and run, so the edges grow with the members and the waiters, not their product. */
static void
scope_report_completion_sources(const async_scope_t *scope, async_collector_t *collector, const uint32_t node)
{
	for (uint32_t i = 0; i < scope->coroutines.length; i++) {
		async_collector_report_reach_source(collector, &scope->coroutines.data[i]->std, node);
	}

	for (uint32_t i = 0; i < scope->child_scopes.length; i++) {
		scope_report_completion_reach(scope->child_scopes.data[i], collector, node);
	}
}

static void scope_record_collector_target(const async_coroutine_event_callback_t *record, async_collector_t *collector)
{
	const async_scope_t *scope = (const async_scope_t *) record->event;
	bool is_new_node;
	const uint32_t node = async_collector_report_reach_target(collector, &scope->event, &is_new_node);

	if (is_new_node) {
		scope_report_completion_sources(scope, collector, node);
	}
}

/* The child scopes' objects: the parent's disposal waits until its last child scope is gone (scope_dispose()),
 * and releasing a child's object disposes it, cancelled or not. Once a node per scope and run. */
static void
scope_report_child_scope_holders(const async_scope_t *scope, async_collector_t *collector, const uint32_t node)
{
	bool is_new_node;
	const uint32_t holders_node = async_collector_reach_node(collector, &scope->child_scopes, &is_new_node);

	async_collector_report_reach(collector, holders_node, node);

	if (EXPECTED(!is_new_node)) {
		return;
	}

	for (uint32_t i = 0; i < scope->child_scopes.length; i++) {
		const async_scope_t *const child_scope = scope->child_scopes.data[i];

		if (child_scope->scope_object != NULL) {
			async_collector_report_holder(collector, child_scope->scope_object, holders_node);
		}

		scope_report_child_scope_holders(child_scope, collector, holders_node);
	}
}

void async_scope_collector_bound_channel_reach(async_collector_t *collector, async_scope_t *scope, const uint32_t node)
{
	scope_collector_reach_from(collector, scope, node);

	const bool is_cancelled = (scope->event.flags & ASYNC_SCOPE_F_CANCELLED) != 0;

	if (scope->scope_object != NULL && !is_cancelled) {
		return;
	}

	/* Its last member's end frees it, through the completion node its awaiters share, or the release of a
	 * child scope's object. */
	scope_report_completion_reach(scope, collector, node);
	scope_report_child_scope_holders(scope, collector, node);

	/* The fire of a cancelled scope's timer closes it, which closes its channels. */
	if (UNEXPECTED(is_cancelled && scope_dispose_timer_is_armed(scope))) {
		async_collector_report_live_reach(collector, node);
	}
}

static const async_wait_kind_t async_wait_kind_scope = {
	.info = scope_record_info,
	.collector_target = scope_record_collector_target,
};

/* Throws for a waiter that belongs to `scope` or to one of its child scopes, which the wait would wait
 * for: the waiter's own scopes, walked up where TrueAsync walks the scope's subtree down
 * (scope.c:866-894). */
static bool scope_refuses_waiter(const async_scope_t *scope, const async_coroutine_t *waiter)
{
	for (const async_scope_t *waiter_scope = waiter->scope; waiter_scope != NULL;
		 waiter_scope = waiter_scope->parent_scope) {
		if (UNEXPECTED(waiter_scope == scope)) {
			zend_throw_exception(async_ce_async_exception,
								 "Cannot await completion of scope from a coroutine that belongs to the same scope or "
								 "its children",
								 0);
			return true;
		}
	}

	return false;
}

/* TrueAsync's awaitCompletion (scope.c:299-372): until no coroutine of the scope or of its child scopes
 * runs, zombies aside. */
ZEND_METHOD(Async_Scope, awaitCompletion)
{
	zend_object *cancellation;

	ZEND_PARSE_PARAMETERS_START(1, 1)
		Z_PARAM_OBJ_OF_CLASS(cancellation, async_ce_awaitable)
	ZEND_PARSE_PARAMETERS_END();

	async_awaitable_t *token = async_await_awaitable_of(cancellation);

	if (UNEXPECTED(token == NULL)) {
		RETURN_THROWS();
	}

	/* Observed before any return, as TrueAsync (scope.c:308-311); a coroutine token's outcome only
	 * when it is read, as for await(). */
	if (cancellation->ce == async_ce_future) {
		((async_event_t *) token)->flags |= ASYNC_EVENT_F_RESULT_USED | ASYNC_EVENT_F_EXC_CAUGHT;
	}

	async_coroutine_t *waiter = (async_coroutine_t *) ZEND_ASYNC_CURRENT_COROUTINE;
	const async_scope_object_t *scope_object = THIS_SCOPE_OBJECT;
	async_scope_t *scope = scope_object->scope;

	if (UNEXPECTED(waiter == NULL || scope == NULL || (scope->event.flags & ASYNC_SCOPE_F_CLOSED))) {
		return;
	}

	if (UNEXPECTED(scope->event.flags & ASYNC_SCOPE_F_CANCELLED)) {
		zend_throw_exception(async_ce_cancellation, "The scope has been cancelled", 0);
		RETURN_THROWS();
	}

	if (UNEXPECTED(scope_refuses_waiter(scope, waiter))) {
		RETURN_THROWS();
	}

	if (scope_is_completed(scope, false, NULL)) {
		return;
	}

	/* A finished coroutine is still current while finalize releases what it held (a destructor, a
	 * scope's exception handler). */
	if (UNEXPECTED(ZEND_COROUTINE_IS_FINISHED(&waiter->coroutine) || ZEND_ASYNC_IN_SCHEDULER_CONTEXT)) {
		zend_throw_error(NULL, "awaitCompletion() requires a running coroutine");
		RETURN_THROWS();
	}

	/* The wait's own reference, as await()'s. */
	async_awaitable_addref(token);
	async_wait_end(waiter);

	/* Another enqueue than the scope's wakes the waiter early: it waits again. */
	do {
		if (UNEXPECTED(!async_await_token_check(token))) {
			break;
		}

		async_callbacks_reserve(&scope->event.callbacks, 1);
		async_callbacks_reserve(async_awaitable_callbacks(token), 1);

		if (UNEXPECTED(!async_await_token_arm(token))) {
			break;
		}

		async_wait_link(&waiter->waker.records[0],
						waiter,
						(async_awaitable_t *) &scope->event,
						&async_wait_kind_scope,
						scope_record_wake);
		async_await_token_link(&waiter->waker.records[1], waiter, token);

		if (UNEXPECTED(!ZEND_ASYNC_SUSPEND())) {
			break;
		}

		scope = scope_object->scope;
	} while (scope != NULL && !scope_is_completed(scope, false, NULL));

	async_awaitable_release(token);
}

/* The wake of a waiter in awaitAfterCancellation(). An error the route brings goes to its waker, as
 * awaitCompletion()'s; a member's end wakes it only once the subtree has no coroutine left, or as the
 * scope's teardown fires the record (`event` NULL), when the child scopes are freed already. The
 * enqueue unlinks the wait (D26). */
static void scope_after_cancellation_record_wake(async_awaitable_t *target,
												 async_event_callback_t *callback,
												 void *result,
												 zend_object *exception)
{
	(void) result;

	const async_coroutine_event_callback_t *record = (async_coroutine_event_callback_t *) callback;

	if (exception == NULL && record->event != NULL && scope_has_coroutines((const async_scope_t *) target)) {
		return;
	}

	if (exception != NULL) {
		GC_ADDREF(exception);
	}

	async_scheduler_enqueue(&record->coroutine->coroutine, exception, true);
}

/* TrueAsync's awaitAfterCancellation (scope.c:374-483), until no coroutine of the subtree is left
 * (S9-scope.md 9, item 17). The handler runs in the waiter, since a notify runs in scheduler context. */
ZEND_METHOD(Async_Scope, awaitAfterCancellation)
{
	zend_fcall_info error_handler = empty_fcall_info;
	zend_fcall_info_cache error_handler_cache = empty_fcall_info_cache;
	zend_object *cancellation = NULL;

	ZEND_PARSE_PARAMETERS_START(0, 2)
		Z_PARAM_OPTIONAL
		Z_PARAM_FUNC_OR_NULL(error_handler, error_handler_cache)
		Z_PARAM_OBJ_OF_CLASS_OR_NULL(cancellation, async_ce_awaitable)
	ZEND_PARSE_PARAMETERS_END();

	async_awaitable_t *token = NULL;

	if (cancellation != NULL) {
		token = async_await_awaitable_of(cancellation);

		if (UNEXPECTED(token == NULL)) {
			RETURN_THROWS();
		}

		if (cancellation->ce == async_ce_future) {
			((async_event_t *) token)->flags |= ASYNC_EVENT_F_RESULT_USED | ASYNC_EVENT_F_EXC_CAUGHT;
		}
	}

	async_coroutine_t *waiter = (async_coroutine_t *) ZEND_ASYNC_CURRENT_COROUTINE;
	const async_scope_object_t *scope_object = THIS_SCOPE_OBJECT;
	async_scope_t *scope = scope_object->scope;

	if (UNEXPECTED(waiter == NULL || scope == NULL)) {
		return;
	}

	/* As TrueAsync's, a closed scope that was not cancelled returns at once, though its cancelled child
	 * scopes may have zombies: a member's end there notifies it only through scopes that completed. */
	if (UNEXPECTED(!(scope->event.flags & ASYNC_SCOPE_F_CANCELLED))) {
		if (scope->event.flags & ASYNC_SCOPE_F_CLOSED) {
			return;
		}

		zend_throw_exception(async_ce_async_exception, "Attempt to await a Scope that has not been cancelled", 0);
		RETURN_THROWS();
	}

	if (UNEXPECTED(scope_refuses_waiter(scope, waiter))) {
		RETURN_THROWS();
	}

	if (!scope_has_coroutines(scope)) {
		return;
	}

	if (UNEXPECTED(ZEND_COROUTINE_IS_FINISHED(&waiter->coroutine) || ZEND_ASYNC_IN_SCHEDULER_CONTEXT)) {
		zend_throw_error(NULL, "awaitAfterCancellation() requires a running coroutine");
		RETURN_THROWS();
	}

	if (token != NULL) {
		async_awaitable_addref(token);
	}

	async_wait_end(waiter);

	/* Each error wakes the waiter, which waits again after its handler while the subtree has a
	 * coroutine. */
	do {
		if (token != NULL && UNEXPECTED(!async_await_token_check(token))) {
			break;
		}

		async_callbacks_reserve(&scope->event.callbacks, 1);

		if (token != NULL) {
			async_callbacks_reserve(async_awaitable_callbacks(token), 1);

			if (UNEXPECTED(!async_await_token_arm(token))) {
				break;
			}
		}

		async_wait_link(&waiter->waker.records[0],
						waiter,
						(async_awaitable_t *) &scope->event,
						&async_wait_kind_scope,
						scope_after_cancellation_record_wake);

		if (token != NULL) {
			async_await_token_link(&waiter->waker.records[1], waiter, token);
		}

		if (UNEXPECTED(!ZEND_ASYNC_SUSPEND())) {
			zend_object *error = EG(exception);

			/* A cancel or the token always brings a cancellation, on top of a routed error it finds
			 * pending (scheduler.c, waker_apply_error), and an exit replaces it. The route brings one
			 * only when a scope's exception handler threw it in place of the error: it is thrown too. */
			if (!ZEND_FCI_INITIALIZED(error_handler) || instanceof_function(error->ce, async_ce_cancellation) ||
				async_is_exit_object(error)) {
				break;
			}

			GC_ADDREF(error);
			zend_clear_exception();

			zval arguments[2];
			zval retval;

			ZVAL_OBJ(&arguments[0], error);
			ZVAL_OBJ(&arguments[1], Z_OBJ_P(ZEND_THIS));
			ZVAL_UNDEF(&retval);
			error_handler.param_count = 2;
			error_handler.params = arguments;
			error_handler.retval = &retval;
			zend_call_function(&error_handler, &error_handler_cache);
			zval_ptr_dtor(&retval);
			OBJ_RELEASE(error);

			if (UNEXPECTED(EG(exception) != NULL)) {
				break;
			}
		}

		scope = scope_object->scope;
	} while (scope != NULL && scope_has_coroutines(scope));

	if (token != NULL) {
		async_awaitable_release(token);
	}
}

ZEND_METHOD(Async_Scope, isFinished)
{
	ZEND_PARSE_PARAMETERS_NONE();

	const async_scope_t *scope = THIS_SCOPE_OBJECT->scope;

	RETURN_BOOL(scope == NULL || scope_is_completed(scope, false, NULL));
}

ZEND_METHOD(Async_Scope, isClosed)
{
	ZEND_PARSE_PARAMETERS_NONE();

	const async_scope_t *scope = THIS_SCOPE_OBJECT->scope;

	RETURN_BOOL(scope == NULL || (scope->event.flags & ASYNC_SCOPE_F_CLOSED));
}

ZEND_METHOD(Async_Scope, isCancelled)
{
	ZEND_PARSE_PARAMETERS_NONE();

	const async_scope_object_t *scope_object = THIS_SCOPE_OBJECT;

	if (scope_object->scope == NULL) {
		RETURN_BOOL(scope_object->is_cancelled);
	}

	RETURN_BOOL(scope_object->scope->event.flags & ASYNC_SCOPE_F_CANCELLED);
}

/* TrueAsync's METHOD(finally), scope.c:581-625: once the scope is gone the closure is called at once, in
 * the caller, which gets what it throws. */
ZEND_METHOD(Async_Scope, finally)
{
	zval *finally_handler;

	ZEND_PARSE_PARAMETERS_START(1, 1)
		Z_PARAM_OBJECT_OF_CLASS(finally_handler, zend_ce_closure)
	ZEND_PARSE_PARAMETERS_END();

	async_scope_t *scope = THIS_SCOPE_OBJECT->scope;

	if (UNEXPECTED(scope == NULL)) {
		zval retval;

		call_user_function(NULL, NULL, finally_handler, &retval, 1, ZEND_THIS);
		zval_ptr_dtor(&retval);
		return;
	}

	if (scope->finally_handlers == NULL) {
		scope->finally_handlers = zend_new_array(1);
	}

	Z_ADDREF_P(finally_handler);
	zend_hash_next_index_insert_new(scope->finally_handlers, finally_handler);
}

ZEND_METHOD(Async_Scope, getChildScopes)
{
	ZEND_PARSE_PARAMETERS_NONE();

	const async_scope_t *scope = THIS_SCOPE_OBJECT->scope;

	if (scope == NULL) {
		RETURN_EMPTY_ARRAY();
	}

	array_init_size(return_value, scope->child_scopes.length);

	for (uint32_t i = 0; i < scope->child_scopes.length; i++) {
		zend_object *child_object = scope->child_scopes.data[i]->scope_object;

		if (child_object != NULL) {
			GC_ADDREF(child_object);
			add_next_index_object(return_value, child_object);
		}
	}
}

///////////////////////////////////////////////////////////////////
/// spawn_with()
///////////////////////////////////////////////////////////////////

/* What `provider` names, with `scope_value` holding the object it returned until the spawn is done: a
 * Scope nobody else holds would be disposed under it. The current scope for null; NULL with an
 * exception for anything else (TrueAsync's async_provide_scope, async_API.c:32-58) or a Scope that is
 * gone. */
static async_scope_t *scope_provide(zend_object *provider, zval *scope_value)
{
	zend_call_method(provider, provider->ce, NULL, ZEND_STRL("provideScope"), scope_value, 0, NULL, NULL);

	if (UNEXPECTED(EG(exception) != NULL)) {
		return NULL;
	}

	/* A provideScope() declared to return by reference returns a reference, which TrueAsync rejects. */
	if (UNEXPECTED(Z_ISREF_P(scope_value))) {
		zend_unwrap_reference(scope_value);
	}

	if (Z_TYPE_P(scope_value) == IS_NULL) {
		return async_scope_current();
	}

	if (UNEXPECTED(Z_TYPE_P(scope_value) != IS_OBJECT ||
				   !instanceof_function(Z_OBJCE_P(scope_value), async_ce_scope))) {
		zend_throw_exception(async_ce_async_exception, "Scope provider must return an instance of Async\\Scope", 0);
		return NULL;
	}

	async_scope_t *scope = async_scope_object_from_object(Z_OBJ_P(scope_value))->scope;

	if (UNEXPECTED(scope == NULL)) {
		zend_throw_exception(async_ce_async_exception, "Scope object has been disposed", 0);
	}

	return scope;
}

ZEND_FUNCTION(Async_spawn_with)
{
	zend_object *provider = NULL;
	zend_fcall_info fci;
	zend_fcall_info_cache fcc;
	zval *args = NULL;
	uint32_t args_count = 0;
	HashTable *named_args = NULL;

	THROW_IF_UNAVAILABLE();

	ZEND_PARSE_PARAMETERS_START(2, -1)
		Z_PARAM_OBJ_OF_CLASS(provider, async_ce_scope_provider)
		Z_PARAM_FUNC_NO_TRAMPOLINE_FREE(fci, fcc)
		Z_PARAM_VARIADIC_WITH_NAMED(args, args_count, named_args)
	ZEND_PARSE_PARAMETERS_END();

	zval scope_value;
	async_scope_t *scope = scope_provide(provider, &scope_value);
	async_coroutine_t *coroutine = NULL;

	if (EXPECTED(scope != NULL)) {
		zend_object *spawn_strategy = instanceof_function(provider->ce, async_ce_spawn_strategy) ? provider : NULL;

#ifdef TRUE_ASYNC_TEST_HOOKS
		if (spawn_strategy != NULL && Z_TYPE(scope_value) == IS_NULL) {
			scope_hand_out_found(scope);
		}
#endif

		coroutine = async_scope_spawn(scope, spawn_strategy, &fci, &fcc, args, args_count, named_args);
	} else {
		zend_release_fcall_info_cache(&fcc);
	}

	zval_ptr_dtor(&scope_value);

	if (UNEXPECTED(coroutine == NULL)) {
		RETURN_THROWS();
	}

	RETURN_OBJ(&coroutine->std);
}

///////////////////////////////////////////////////////////////////
/// The request
///////////////////////////////////////////////////////////////////

void async_scope_request_startup(void)
{
	ASYNC_G(global_scope) = async_scope_new(NULL);
	ASYNC_G(global_scope)->event.flags |= ASYNC_SCOPE_F_DISPOSE_SAFELY | ASYNC_SCOPE_F_REQUEST_LIFETIME;
	ASYNC_G(engine_scope) = async_scope_new(NULL);
	ASYNC_G(engine_scope)->event.flags |= ASYNC_SCOPE_F_REQUEST_LIFETIME;
	ASYNC_G(zombie_coroutines_count) = 0;
}

void async_scope_request_shutdown(zend_array **released_values)
{
	async_coroutine_t *coroutine = NULL;

	ZEND_HASH_FOREACH_PTR(&ASYNC_G(coroutines), coroutine)
	{
		if (coroutine->scope != NULL) {
			scope_remove_coroutine(coroutine, released_values);
		}
	}
	ZEND_HASH_FOREACH_END();

	scope_free(ASYNC_G(global_scope), released_values);
	scope_free(ASYNC_G(engine_scope), released_values);
	ASYNC_G(global_scope) = NULL;
	ASYNC_G(engine_scope) = NULL;
}

void async_register_scope_ce(void)
{
	async_ce_scope_provider = register_class_Async_ScopeProvider();
	async_ce_spawn_strategy = register_class_Async_SpawnStrategy(async_ce_scope_provider);
	async_ce_scope = register_class_Async_Scope(async_ce_scope_provider);
	async_ce_scope->create_object = scope_object_create;
	async_ce_scope->default_object_handlers = &scope_handlers;

	memcpy(&scope_handlers, zend_get_std_object_handlers(), sizeof(zend_object_handlers));
	scope_handlers.offset = offsetof(async_scope_object_t, std);
	scope_handlers.dtor_obj = scope_object_destroy;
	scope_handlers.free_obj = scope_object_free;
	scope_handlers.get_gc = scope_object_get_gc;
	scope_handlers.clone_obj = NULL;
}
