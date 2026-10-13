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
#include "iterator.h"
#include "coroutine.h"
#include "exceptions.h"
#include "io_provider.h"
#include "scheduler.h"
#include "scope.h"

/* The port of TrueAsync's iterator.c (S9-scope.md 7) to this extension's contracts: the defer slot takes
 * the caller's reference to the microtask and its release frees it after `dtor`, which here only lets go
 * of what the iterator holds; a worker that never ran is let go of by its finish handler, where
 * TrueAsync's extended_dispose runs at the coroutine's finish. Unlike TrueAsync, the last worker that ran
 * releases what the walk holds unless an exit is pending (S9-scope.md 15). */

static void iterator_worker_entry(void);

/* Queues the microtask with a reference of its own (TrueAsync's ZEND_ASYNC_ADD_MICROTASK). */
static void iterator_defer(async_iterator_t *iterator)
{
	ZEND_ASYNC_MICROTASK_ADDREF(&iterator->microtask);

	if (UNEXPECTED(!ZEND_ASYNC_DEFER(&iterator->microtask))) {
		iterator->microtask.ref_count--;
	}
}

/* Lets go of what the walk holds and what a caller's larger struct holds; each is detached first, so a second
 * call finds nothing. The last worker that ran calls it in its body, where a destructor may suspend; otherwise
 * the microtask's release does (the worker was cancelled before it ran, none was spawned, or an exit is
 * pending). */
static void iterator_dispose(async_iterator_t *iterator)
{
	if (iterator->extended_dtor != NULL) {
		const async_iterator_dtor_t extended_dtor = iterator->extended_dtor;
		iterator->extended_dtor = NULL;
		extended_dtor(iterator);
	}

	if (iterator->hash_iterator != (uint32_t) -1) {
		zend_hash_iterator_del(iterator->hash_iterator);
		iterator->hash_iterator = (uint32_t) -1;
	}

	if (!Z_ISUNDEF(iterator->array)) {
		zval array;
		ZVAL_COPY_VALUE(&array, &iterator->array);
		ZVAL_UNDEF(&iterator->array);
		zval_ptr_dtor(&array);
	}

	if (iterator->zend_iterator != NULL) {
		zend_object_iterator *const zend_iterator = iterator->zend_iterator;
		iterator->zend_iterator = NULL;
		zend_iterator_dtor(zend_iterator);
	}

	if (iterator->fcall != NULL) {
		zend_fcall_t *const fcall = iterator->fcall;
		iterator->fcall = NULL;
		ZEND_ASYNC_FCALL_FREE(fcall);
	}
}

/* The last worker to leave ends with the iterator's exception, which then goes its route as that
 * worker's error, a cancellation it was given before chained as its previous. One that ran throws it in
 * its body, where an exit stays and an error a destructor of iterator_dispose() threw goes over it, as PHP
 * chains a destructor's error; one that never ran has no body, so its finish handler makes the error its
 * outcome, which finalize takes after the notify. */
static void iterator_end_worker(async_iterator_t *iterator, async_coroutine_t *worker, const bool has_run)
{
	zend_object *exception = iterator->exception;

	if (EXPECTED(exception == NULL)) {
		return;
	}

	iterator->exception = NULL;

	if (EXPECTED(has_run)) {
		zend_object *previous = EG(exception);

		if (UNEXPECTED(previous != NULL)) {
			if (UNEXPECTED(async_is_exit_object(previous))) {
				OBJ_RELEASE(exception);
				return;
			}

			GC_ADDREF(previous);
			zend_clear_exception();

			if (UNEXPECTED(!instanceof_function(previous->ce, async_ce_cancellation))) {
				zend_exception_set_previous(previous, exception);
				EG(exception) = previous;
				return;
			}

			zend_exception_set_previous(exception, previous);
		}

		EG(exception) = exception;
		return;
	}

	zend_coroutine_t *zend_worker = &worker->coroutine;

	zend_exception_set_previous(exception, zend_worker->exception);
	zend_worker->exception = exception;
}

/* Gives back the slot a worker took at its spawn; the last one to leave finishes the iterator. */
static void iterator_release_coroutine(async_iterator_t *iterator, async_coroutine_t *worker, const bool has_run)
{
	if (iterator->active_coroutines > 1) {
		iterator->active_coroutines--;
		return;
	}

	iterator->active_coroutines = 0;
	iterator->state = ASYNC_ITERATOR_FINISHED;

	/* Before the walk's error is thrown, so that a cancellation that ends a destructor's wait goes under it,
	 * and before the run ends, so that no scope cancel interrupts the wait, as none interrupts a handler
	 * (S9-scope.md 15). After an exit the tick lets go, so that a destructor's error, which PHP chains over a
	 * pending exception, cannot take the exit's place as the worker's outcome. */
	if (EXPECTED(has_run)) {
		if (UNEXPECTED(EG(exception) != NULL && async_is_exit_object(EG(exception)))) {
			iterator_defer(iterator);
		} else {
			iterator_dispose(iterator);
		}
	}

	/* The leaving worker still keeps the scope alive here. */
	async_scope_finally_run_end(iterator->scope);

	if (iterator->end_handler != NULL) {
		iterator->end_handler(iterator, worker, has_run);
		return;
	}

	iterator_end_worker(iterator, worker, has_run);
}

/* Lets go of a worker that never ran, whose entry would have taken `extended_data`: its slot and its
 * reference go here. After a bailout nothing more runs, and the iterator's exception goes with it. */
static bool iterator_worker_finished(zend_coroutine_t *coroutine, zend_coroutine_t *waiter, void *data, bool is_bailout)
{
	(void) waiter;
	(void) data;

	async_iterator_t *iterator = coroutine->extended_data;

	if (EXPECTED(iterator == NULL)) {
		return false;
	}

	coroutine->extended_data = NULL;

	if (UNEXPECTED(is_bailout) && iterator->exception != NULL) {
		zend_object *exception = iterator->exception;
		iterator->exception = NULL;
		OBJ_RELEASE(exception);
	}

	iterator_release_coroutine(iterator, (async_coroutine_t *) coroutine, false);
	ZEND_ASYNC_MICROTASK_RELEASE(&iterator->microtask);

	return false;
}

/* A worker in the iterator's scope, which takes a slot and a reference of the microtask; NULL when the
 * scope is closed or the scheduler refuses it. */
static async_coroutine_t *iterator_worker_spawn(async_iterator_t *iterator)
{
	if (UNEXPECTED(iterator->scope->event.flags & ASYNC_SCOPE_F_CLOSED)) {
		return NULL;
	}

	ASYNC_IO_PROVIDER_INSTALL_ONCE();

	async_coroutine_t *worker = async_coroutine_new();

	worker->coroutine.internal_entry = iterator_worker_entry;

	if (iterator->is_hi_priority) {
		worker->coroutine.flags |= ASYNC_COROUTINE_F_HI_PRIORITY;
	}

	async_scope_add_coroutine(iterator->scope, worker);

	if (UNEXPECTED(!async_scheduler_enqueue(&worker->coroutine, NULL, false))) {
		async_scope_discard_coroutine(worker);
		return NULL;
	}

	worker->coroutine.extended_data = iterator;
	async_finish_handler_add(&worker->coroutine, iterator_worker_finished, NULL, NULL);
	iterator->active_coroutines++;

	return worker;
}

/* Spawns one more worker while the walk goes on and the limit allows (TrueAsync's iterator_microtask). */
static void iterator_microtask(zend_async_microtask_t *microtask)
{
	async_iterator_t *iterator = (async_iterator_t *) microtask;

	if (iterator->state == ASYNC_ITERATOR_FINISHED ||
		(iterator->concurrency > 0 && iterator->active_coroutines >= iterator->concurrency)) {
		return;
	}

	if (EXPECTED(iterator_worker_spawn(iterator) != NULL)) {
		ZEND_ASYNC_MICROTASK_ADDREF(microtask);
	}
}

static void iterator_dtor(zend_async_microtask_t *microtask)
{
	async_iterator_t *iterator = (async_iterator_t *) microtask;

	iterator_dispose(iterator);

	if (iterator->exception != NULL) {
		zend_object *exception = iterator->exception;
		iterator->exception = NULL;
		OBJ_RELEASE(exception);
	}
}

/* The Traversable is moved by one worker at a time: a move may run PHP code that suspends, and the
 * microtask spawns no worker meanwhile. A microtask the tick dropped while it was cancelled is queued
 * again; the cancel is lifted, or no worker would be spawned after a move that did not suspend. A walk
 * another worker stopped during the move stays stopped (S9-scope.md 9, item 22). */
#define ITERATOR_SAFE_MOVING_START(iterator) \
	(iterator)->state = ASYNC_ITERATOR_MOVING; \
	(iterator)->microtask.is_cancelled = true; \
	const uint32_t references_before_move = (iterator)->microtask.ref_count;

#define ITERATOR_SAFE_MOVING_END(iterator) \
	if (EXPECTED((iterator)->state == ASYNC_ITERATOR_MOVING)) { \
		(iterator)->state = ASYNC_ITERATOR_STARTED; \
		(iterator)->microtask.is_cancelled = false; \
		if (references_before_move != (iterator)->microtask.ref_count) { \
			iterator_defer(iterator); \
		} \
	}

#define ITERATOR_FINISH(iterator) \
	do { \
		(iterator)->state = ASYNC_ITERATOR_FINISHED; \
		(iterator)->microtask.is_cancelled = true; \
	} while (0)

async_iterator_t *async_iterator_new(zval *array,
									 zend_object_iterator *zend_iterator,
									 zend_fcall_t *fcall,
									 async_iterator_handler_t handler,
									 async_scope_t *scope,
									 const unsigned int concurrency,
									 const bool is_hi_priority,
									 size_t iterator_size)
{
	ZEND_ASSERT((array != NULL) != (zend_iterator != NULL) && (fcall != NULL) != (handler != NULL));

	if (iterator_size == 0) {
		iterator_size = sizeof(async_iterator_t);
	}

	async_iterator_t *iterator = ecalloc(1, iterator_size);

	iterator->microtask.handler = iterator_microtask;
	iterator->microtask.dtor = iterator_dtor;
	iterator->microtask.ref_count = 1;
	iterator->hash_iterator = (uint32_t) -1;
	iterator->state = ASYNC_ITERATOR_INIT;
	iterator->concurrency = concurrency;
	iterator->is_hi_priority = is_hi_priority;
	iterator->scope = scope;
	iterator->fcall = fcall;
	iterator->handler = handler;
	iterator->zend_iterator = zend_iterator;
	ZVAL_UNDEF(&iterator->array);

	if (array != NULL) {
		ZVAL_COPY(&iterator->array, array);
	}

	return iterator;
}

/* The walk of one worker (TrueAsync's iterate()): it takes the next element, moves the position on and
 * calls the handler, until the elements end, a handler returns false, fails or throws, or another worker
 * is moving the Traversable. The position is shared, so the workers take the elements in turn. */
static void iterator_walk(async_iterator_t *iterator)
{
	if (UNEXPECTED(iterator->state == ASYNC_ITERATOR_MOVING)) {
		return;
	}

	zend_result result = SUCCESS;
	zval retval;
	zend_fcall_info fci;

	ZVAL_UNDEF(&retval);
	fci.params = NULL;

	/* A copy of the call: a handler may start another worker, which makes its own. */
	if (iterator->fcall != NULL) {
		fci = iterator->fcall->fci;
		fci.param_count = 2;
		fci.params = safe_emalloc(2, sizeof(zval), 0);
		ZVAL_UNDEF(&fci.params[0]);
		ZVAL_UNDEF(&fci.params[1]);
		fci.retval = &retval;
	}

	if (iterator->zend_iterator == NULL) {
		if (iterator->hash_iterator == (uint32_t) -1) {
			ZEND_ASSERT(!(GC_FLAGS(Z_ARRVAL(iterator->array)) & GC_IMMUTABLE));
			zend_hash_internal_pointer_reset_ex(Z_ARRVAL(iterator->array), &iterator->position);
			iterator->hash_iterator = zend_hash_iterator_add(Z_ARRVAL(iterator->array), iterator->position);
		}

		/* As TrueAsync (iterator.c:349), for an array a handler changed. Kept for parity: our callers hand
		 * in arrays no PHP code can change. */
		iterator->position = zend_hash_iterator_pos_ex(iterator->hash_iterator, &iterator->array);
		iterator->target_hash = Z_ARRVAL(iterator->array);
	} else if (iterator->state == ASYNC_ITERATOR_INIT) {
		iterator->state = ASYNC_ITERATOR_STARTED;

		if (iterator->zend_iterator->funcs->rewind != NULL) {
			ITERATOR_SAFE_MOVING_START(iterator);
			iterator->zend_iterator->funcs->rewind(iterator->zend_iterator);
			ITERATOR_SAFE_MOVING_END(iterator);
		}

		if (UNEXPECTED(EG(exception) != NULL)) {
			ITERATOR_FINISH(iterator);
		}
	}

	zval current_item;
	zval key;

	while (iterator->state != ASYNC_ITERATOR_FINISHED && iterator->state != ASYNC_ITERATOR_MOVING) {
		zval *current = NULL;

		ZVAL_UNDEF(&current_item);

		if (iterator->target_hash != NULL) {
			current = zend_hash_get_current_data_ex(iterator->target_hash, &iterator->position);
		} else {
			ITERATOR_SAFE_MOVING_START(iterator);
			const zend_result valid = iterator->zend_iterator->funcs->valid(iterator->zend_iterator);

			if (EXPECTED(valid == SUCCESS && EG(exception) == NULL)) {
				current = iterator->zend_iterator->funcs->get_current_data(iterator->zend_iterator);
			}

			ITERATOR_SAFE_MOVING_END(iterator);

			if (UNEXPECTED(EG(exception) != NULL || iterator->state == ASYNC_ITERATOR_FINISHED)) {
				ITERATOR_FINISH(iterator);
				break;
			}

			if (EXPECTED(current != NULL)) {
				ZVAL_COPY(&current_item, current);
				current = &current_item;
			}
		}

		if (UNEXPECTED(current == NULL)) {
			ITERATOR_FINISH(iterator);
			break;
		}

		/* An undefined slot of an object's properties table is skipped. */
		if (UNEXPECTED(Z_TYPE_P(current) == IS_INDIRECT)) {
			current = Z_INDIRECT_P(current);

			if (Z_TYPE_P(current) == IS_UNDEF) {
				if (iterator->target_hash != NULL) {
					zend_hash_move_forward_ex(iterator->target_hash, &iterator->position);
					continue;
				}

				ITERATOR_SAFE_MOVING_START(iterator);
				iterator->zend_iterator->funcs->move_forward(iterator->zend_iterator);
				iterator->zend_iterator->index++;
				ITERATOR_SAFE_MOVING_END(iterator);

				if (UNEXPECTED(EG(exception) != NULL || iterator->state == ASYNC_ITERATOR_FINISHED)) {
					ITERATOR_FINISH(iterator);
					break;
				}

				continue;
			}
		}

		if (iterator->target_hash != NULL) {
			zend_hash_get_current_key_zval_ex(iterator->target_hash, &key, &iterator->position);
		} else {
			ITERATOR_SAFE_MOVING_START(iterator);

			if (iterator->zend_iterator->funcs->get_current_key != NULL) {
				iterator->zend_iterator->funcs->get_current_key(iterator->zend_iterator, &key);
			} else {
				ZVAL_LONG(&key, iterator->zend_iterator->index);
			}

			ITERATOR_SAFE_MOVING_END(iterator);

			if (UNEXPECTED(EG(exception) != NULL || iterator->state == ASYNC_ITERATOR_FINISHED)) {
				ITERATOR_FINISH(iterator);
				zval_ptr_dtor(&current_item);
				zval_ptr_dtor(&key);
				break;
			}
		}

		/* The next element before the call, as foreach does: while the handler waits, another worker takes
		 * the next one. */
		if (iterator->target_hash != NULL) {
			zend_hash_move_forward_ex(iterator->target_hash, &iterator->position);
			EG(ht_iterators)[iterator->hash_iterator].pos = iterator->position;
		} else {
			ITERATOR_SAFE_MOVING_START(iterator);
			iterator->zend_iterator->funcs->move_forward(iterator->zend_iterator);
			iterator->zend_iterator->index++;
			ITERATOR_SAFE_MOVING_END(iterator);

			if (UNEXPECTED(EG(exception) != NULL || iterator->state == ASYNC_ITERATOR_FINISHED)) {
				ITERATOR_FINISH(iterator);
				zval_ptr_dtor(&current_item);
				zval_ptr_dtor(&key);
				break;
			}
		}

		if (iterator->fcall != NULL) {
			ZVAL_COPY(&fci.params[0], current);
			ZVAL_COPY_VALUE(&fci.params[1], &key);
			ZVAL_UNDEF(&key);
			result = zend_call_function(&fci, &iterator->fcall->fci_cache);
			zval_ptr_dtor(&fci.params[0]);
			zval_ptr_dtor(&fci.params[1]);
			ZVAL_UNDEF(&fci.params[0]);
			ZVAL_UNDEF(&fci.params[1]);
		} else {
			result = iterator->handler(iterator, current, &key);
		}

		zval_ptr_dtor(&current_item);
		zval_ptr_dtor(&key);

		if (UNEXPECTED(result == FAILURE || EG(exception) != NULL)) {
			zval_ptr_dtor(&retval);
			ITERATOR_FINISH(iterator);
			break;
		}

		if (Z_TYPE(retval) == IS_FALSE) {
			ITERATOR_FINISH(iterator);
		}

		zval_ptr_dtor(&retval);
		ZVAL_UNDEF(&retval);

		if (iterator->target_hash != NULL) {
			iterator->position = zend_hash_iterator_pos_ex(iterator->hash_iterator, &iterator->array);
			iterator->target_hash = Z_ARRVAL(iterator->array);
		}
	}

	if (fci.params != NULL) {
		efree(fci.params);
	}
}

/* The walk's exception becomes the iterator's, chained over an earlier one, and cancels the scope of
 * its workers (TrueAsync's async_iterator_apply_exception). An exit, and a cancellation of the worker
 * itself, stay where they are: they end the worker, not the walk's result. A cancellation the handler
 * threw with its worker not cancelled (an expired timeout()) is the walk's, where TrueAsync drops it
 * (Edmond, 2026-10-13). */
static void iterator_take_exception(async_iterator_t *iterator)
{
	zend_object *exception = EG(exception);

	if (EXPECTED(exception == NULL) || async_is_exit_object(exception) ||
		(instanceof_function(exception->ce, async_ce_cancellation) &&
		 ZEND_COROUTINE_IS_CANCELLED(ZEND_ASYNC_CURRENT_COROUTINE))) {
		return;
	}

	GC_ADDREF(exception);
	zend_clear_exception();

	/* Stored before the chaining, which may release the earlier one and wait for the GC run that starts: another
	 * worker's exception then chains over this one, not over the reference given away. */
	zend_object *const previous_exception = iterator->exception;

	iterator->exception = exception;
	zend_exception_set_previous(exception, previous_exception);

	async_scope_t *scope = iterator->scope;

	if (scope->event.flags & ASYNC_SCOPE_F_CANCELLED) {
		return;
	}

	async_scope_cancel(scope,
					   async_new_exception(async_ce_cancellation, "Cancellation of the iterator due to an exception"),
					   true,
					   (scope->event.flags & ASYNC_SCOPE_F_DISPOSE_SAFELY) != 0);
}

static void iterator_run(async_iterator_t *iterator)
{
	iterator_defer(iterator);
	iterator_walk(iterator);
	iterator_take_exception(iterator);
}

static void iterator_worker_entry(void)
{
	zend_coroutine_t *worker = ZEND_ASYNC_CURRENT_COROUTINE;
	async_iterator_t *iterator = worker->extended_data;

	worker->extended_data = NULL;
	iterator_run(iterator);
	iterator_release_coroutine(iterator, (async_coroutine_t *) worker, true);
	ZEND_ASYNC_MICROTASK_RELEASE(&iterator->microtask);
}

bool async_iterator_run_in_coroutine(async_iterator_t *iterator)
{
	return iterator_worker_spawn(iterator) != NULL;
}

///////////////////////////////////////////////////////////////////
/// iterate()
///////////////////////////////////////////////////////////////////

/* iterate()'s walk (S9-iterate.md 3). The caller holds a reference of its own to the microtask, as TrueAsync's
 * async.c:1066; while it is parked, its waker's block owns that reference. */
typedef struct
{
	async_iterator_t iterator;
	async_wait_block_t block;
	/* The caller's one record, in the scope's callbacks while it is parked. */
	async_coroutine_event_callback_t record;
	/* In the scope's callbacks for the whole call: it takes the route's errors, and a scope without an object goes
	 * with its last coroutine, maybe before the caller runs again, and its free clears `iterator.scope` here. */
	async_event_callback_t scope_callback;
	zend_object *route_error; /* the first error of the scope's route */
	bool cancel_pending;
} iterate_walk_t;

/* A member's end wakes the caller once the subtree has no coroutine left, or as the scope's teardown fires the
 * record (`event` NULL); an error is the subscriber's. The enqueue unlinks the wait (D26). */
static void
iterate_record_wake(async_awaitable_t *target, async_event_callback_t *callback, void *result, zend_object *exception)
{
	(void) result;

	const async_coroutine_event_callback_t *record = (async_coroutine_event_callback_t *) callback;

	if (UNEXPECTED(exception != NULL) ||
		(record->event != NULL && async_scope_has_coroutines((const async_scope_t *) target, NULL))) {
		return;
	}

	async_scheduler_enqueue(&record->coroutine->coroutine, NULL, false);
}

static zend_string *iterate_record_info(const async_coroutine_event_callback_t *record)
{
	const async_scope_t *scope = (const async_scope_t *) record->event;

	if (UNEXPECTED(scope->filename == NULL)) {
		return zend_string_init(ZEND_STRL("iterate()"), 0);
	}

	return zend_strpprintf(0, "iterate() at %s:%" PRIu32, ZSTR_VAL(scope->filename), scope->lineno);
}

/* The coroutines of the iterator's scope end the wait. */
static void iterate_record_collector_target(const async_coroutine_event_callback_t *record,
											async_collector_t *collector)
{
	async_scope_collector_completion_target((const async_scope_t *) record->event, collector);
}

static const async_wait_kind_t iterate_wait_kind = {
	.info = iterate_record_info,
	.collector_target = iterate_record_collector_target,
};

/* As awaitCompletion() takes it, and stops the walk as a callback's exception does. A cancel's notify is left to the
 * caller's waker. */
static void
iterate_scope_error(async_awaitable_t *target, async_event_callback_t *callback, void *result, zend_object *exception)
{
	(void) target;
	(void) result;

	if (exception == NULL || instanceof_function(exception->ce, async_ce_cancellation)) {
		return;
	}

	iterate_walk_t *const walk = (iterate_walk_t *) ((char *) callback - offsetof(iterate_walk_t, scope_callback));

	ITERATOR_FINISH(&walk->iterator);
	GC_ADDREF(exception);

	if (EXPECTED(walk->route_error == NULL)) {
		walk->route_error = exception;
	} else {
		async_unobserved_exception_add(exception);
	}
}

static void iterate_scope_freed(async_event_callback_t *callback, async_awaitable_t *target)
{
	(void) target;

	iterate_walk_t *const walk = (iterate_walk_t *) ((char *) callback - offsetof(iterate_walk_t, scope_callback));

	walk->iterator.scope = NULL;
}

/* After the last callback, the coroutines the callbacks spawned that still run are cancelled with TrueAsync's
 * message when `cancel_pending` (async.c:1083-1094). The last worker gives them one turn first, as TrueAsync's
 * caller runs after the coroutines queued before its completion event (reference test 012); the caller stays
 * parked meanwhile, and the scope's subscriber stops an error they end with. As TrueAsync's, the cancel comes
 * whatever the walk ended with, a cancellation of the worker included; only an exit leaves them to the shutdown's
 * cancel. In a scope cancelled before, a coroutine handling that cancel finishes its cleanup, as TrueAsync cancels
 * nothing there (async.c:1084); the rest, zombies a safe cancel left and coroutines spawned since, are cancelled, or
 * the caller would hang. */
static void iterate_walk_end(async_iterator_t *iterator, async_coroutine_t *worker, const bool has_run)
{
	const iterate_walk_t *walk = (iterate_walk_t *) iterator;
	async_scope_t *const scope = iterator->scope;

	if (!walk->cancel_pending || !async_scope_has_coroutines(scope, worker)) {
		return;
	}

	if (has_run && EG(exception) == NULL && async_scheduler_enqueue(&worker->coroutine, NULL, false)) {
		ZEND_ASYNC_SUSPEND();
	}

	if (UNEXPECTED(EG(exception) != NULL && async_is_exit_object(EG(exception))) ||
		!async_scope_has_coroutines(scope, worker)) {
		return;
	}

	zend_object *worker_exception = NULL;

	async_exception_save_fast(&EG(exception), &worker_exception);

	zend_object *const cancellation =
			async_new_exception(async_ce_cancellation, "Cancellation of pending coroutines after iterator completion");

	if (UNEXPECTED(scope->event.flags & ASYNC_SCOPE_F_CANCELLED)) {
		async_scope_cancel_remaining(scope, cancellation, true);
		OBJ_RELEASE(cancellation);
	} else {
		async_scope_cancel(scope, cancellation, true, false);
	}

	async_exception_restore_fast(&EG(exception), &worker_exception);
}

/* The caller lets go of the walk: the scope keeps what still runs, an exception the walk ends with later goes to
 * its last worker as without an owner, and the caller's reference goes, which may free the walk. */
static void iterate_walk_release(iterate_walk_t *walk)
{
	walk->iterator.end_handler = NULL;

	/* The workers left keep the scope. */
	if (walk->iterator.scope != NULL) {
		async_callbacks_remove(&walk->iterator.scope->event.callbacks, &walk->scope_callback);
	}

	if (UNEXPECTED(walk->route_error != NULL)) {
		zend_object *const route_error = walk->route_error;
		walk->route_error = NULL;
		OBJ_RELEASE(route_error);
	}

	ZEND_ASYNC_MICROTASK_RELEASE(&walk->iterator.microtask);
}

static void iterate_block_unlink(async_wait_block_t *block)
{
	iterate_walk_t *walk = (iterate_walk_t *) ((char *) block - offsetof(iterate_walk_t, block));

	async_wait_record_unlink(&walk->record);
}

/* The caller's frame never runs again (a bailout, the request's end). */
static void iterate_block_release(async_wait_block_t *block)
{
	iterate_walk_release((iterate_walk_t *) ((char *) block - offsetof(iterate_walk_t, block)));
}

static void iterate_block_walk(async_wait_block_t *block,
							   void (*visit)(const async_coroutine_event_callback_t *record, void *arg),
							   void *arg)
{
	const iterate_walk_t *walk = (iterate_walk_t *) ((char *) block - offsetof(iterate_walk_t, block));

	if (walk->record.event != NULL) {
		visit(&walk->record, arg);
	}
}

static const async_wait_block_ops_t iterate_block_ops = {
	.unlink = iterate_block_unlink,
	.release = iterate_block_release,
	.walk = iterate_block_walk,
};

/* Parks the caller until no coroutine of the iterator's scope is left, or its waker brings an error, which stays
 * pending. */
static void iterate_wait(iterate_walk_t *walk, async_coroutine_t *waiter)
{
	async_scope_t *const scope = walk->iterator.scope;

	async_callbacks_reserve(&scope->event.callbacks, 1);
	async_wait_link(
			&walk->record, waiter, (async_awaitable_t *) &scope->event, &iterate_wait_kind, iterate_record_wake);
	waiter->waker.block = &walk->block;

	ZEND_ASYNC_SUSPEND();

	async_wait_take_block(waiter);
}

void async_iterate(async_coroutine_t *waiter,
				   zval *iterable,
				   zend_fcall_t *fcall,
				   const unsigned int concurrency,
				   const bool cancel_pending)
{
	/* First: a block it releases may run a destructor that suspends or throws. */
	async_wait_end(waiter);

	if (UNEXPECTED(EG(exception) != NULL)) {
		ZEND_ASYNC_FCALL_FREE(fcall);
		return;
	}

	zval array;
	zend_object_iterator *zend_iterator = NULL;

	ZVAL_UNDEF(&array);

	/* A copy: the walk's hash iterator never sees the caller's array. */
	if (Z_TYPE_P(iterable) == IS_ARRAY) {
		ZVAL_ARR(&array, zend_array_dup(Z_ARRVAL_P(iterable)));
	} else {
		zend_iterator = Z_OBJCE_P(iterable)->get_iterator(Z_OBJCE_P(iterable), iterable, 0);

		if (UNEXPECTED(zend_iterator == NULL)) {
			if (EG(exception) == NULL) {
				zend_throw_exception(async_ce_async_exception, "Failed to create iterator", 0);
			}

			ZEND_ASYNC_FCALL_FREE(fcall);
			return;
		}
	}

	/* As async.c:1016: every scope inherits DISPOSE_SAFELY from the global one, and a cancel of the walk would
	 * leave its started coroutines running as zombies. */
	async_scope_t *const scope = async_scope_new(async_scope_current());

	scope->event.flags &= ~ASYNC_SCOPE_F_DISPOSE_SAFELY;

	iterate_walk_t *const walk = (iterate_walk_t *) async_iterator_new(zend_iterator == NULL ? &array : NULL,
																	   zend_iterator,
																	   fcall,
																	   NULL,
																	   scope,
																	   concurrency,
																	   false,
																	   sizeof(iterate_walk_t));

	zval_ptr_dtor(&array);

	walk->iterator.end_handler = iterate_walk_end;
	walk->block.ops = &iterate_block_ops;
	walk->cancel_pending = cancel_pending;
	walk->scope_callback.flags = ASYNC_CALLBACK_F_TAKES_ERROR;
	walk->scope_callback.callback = iterate_scope_error;
	walk->scope_callback.dispose = iterate_scope_freed;
	async_callbacks_reserve(&scope->event.callbacks, 1);
	async_callbacks_push_reserved(&scope->event.callbacks, &walk->scope_callback);
	ZEND_ASYNC_MICROTASK_ADDREF(&walk->iterator.microtask);

	if (UNEXPECTED(!async_iterator_run_in_coroutine(&walk->iterator))) {
		/* A refused worker's discard may have freed the scope, which its subscriber records. */
		async_scope_t *const remaining_scope = walk->iterator.scope;

		/* The creator's reference, then the caller's. */
		ZEND_ASYNC_MICROTASK_RELEASE(&walk->iterator.microtask);
		iterate_walk_release(walk);

		if (remaining_scope != NULL) {
			/* The release's destructor error waits: the scope's disposal may run PHP code. */
			zend_object *const release_error = EG(exception);

			EG(exception) = NULL;
			async_scope_release_owner(remaining_scope);
			async_exception_merge(release_error);
		}

		return;
	}

	zend_object *caller_error = NULL;
	zend_object *second_error = NULL;

	/* An enqueue other than the record's wakes the caller early: it waits again. Its first error cancels the
	 * walk, and the caller waits until the scope's coroutines end (S9-iterate.md, question 1); an exit or a
	 * second error leaves at once. */
	while (walk->iterator.scope != NULL && async_scope_has_coroutines(walk->iterator.scope, NULL)) {
		iterate_wait(walk, waiter);

		zend_object *const error = EG(exception);

		if (EXPECTED(error == NULL)) {
			continue;
		}

		GC_ADDREF(error);
		zend_clear_exception();

		if (UNEXPECTED(caller_error != NULL || async_is_exit_object(error))) {
			second_error = error;
			break;
		}

		caller_error = error;
		ITERATOR_FINISH(&walk->iterator);

		if (walk->iterator.scope != NULL) {
			async_scope_cancel_remaining(walk->iterator.scope, caller_error, false);
		}
	}

	zend_object *const walk_exception = walk->iterator.exception;

	walk->iterator.exception = NULL;

	zend_object *const route_error = walk->route_error;

	walk->route_error = NULL;
	iterate_walk_release(walk);

	/* The release may dispose the walk, whose destructors can throw: their error goes under the outcome. */
	zend_object *const release_error = EG(exception);

	EG(exception) = NULL;

	/* As pool_take()'s: the walk's exception over the route's errors, a cancellation of the caller on top,
	 * an exit over all (async.c:1137-1153). */
	async_exception_settle(walk_exception, route_error);

	if (UNEXPECTED(caller_error != NULL)) {
		zend_object *const walk_error = EG(exception);

		EG(exception) = NULL;
		async_exception_settle(caller_error, walk_error);
	}

	/* A second cancel only ends the wait; an exit stays the outcome. */
	if (UNEXPECTED(second_error != NULL && !async_is_exit_object(second_error))) {
		OBJ_RELEASE(second_error);
	} else {
		async_exception_merge(second_error);
	}

	if (UNEXPECTED(release_error != NULL)) {
		zend_object *const outcome = EG(exception);

		EG(exception) = NULL;
		async_exception_settle(outcome, release_error);
	}
}
