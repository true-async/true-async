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
 * TrueAsync's extended_dispose runs at the coroutine's finish. */

static void iterator_worker_entry(void);

/* Queues the microtask with a reference of its own (TrueAsync's ZEND_ASYNC_ADD_MICROTASK). */
static void iterator_defer(async_iterator_t *iterator)
{
	ZEND_ASYNC_MICROTASK_ADDREF(&iterator->microtask);

	if (UNEXPECTED(!ZEND_ASYNC_DEFER(&iterator->microtask))) {
		iterator->microtask.ref_count--;
	}
}

/* The last worker to leave ends with the iterator's exception, which then goes its route as that
 * worker's error, a cancellation it was given before chained as its previous. One that ran throws it in
 * its body, where an exit stays. One that never ran has no body to throw from: its finish handler makes
 * the error its outcome and takes finalize's steps with it, in the notify (S9-scope.md 9, item 9): a
 * cancellation ends there, a scope's handler may take the error, and one nobody holds ends the request. */
static void iterator_end_worker(async_iterator_t *iterator, async_coroutine_t *worker, const bool is_run)
{
	zend_object *exception = iterator->exception;

	if (EXPECTED(exception == NULL)) {
		return;
	}

	iterator->exception = NULL;

	if (is_run) {
		zend_object *previous = EG(exception);

		if (UNEXPECTED(previous != NULL)) {
			if (async_is_exit_object(previous)) {
				OBJ_RELEASE(exception);
				return;
			}

			GC_ADDREF(previous);
			zend_clear_exception();
			zend_exception_set_previous(exception, previous);
		}

		EG(exception) = exception;
		return;
	}

	zend_coroutine_t *zend_worker = &worker->coroutine;

	if (zend_worker->exception != NULL) {
		zend_exception_set_previous(exception, zend_worker->exception);
	}

	zend_worker->exception = exception;

	if (instanceof_function(exception->ce, async_ce_cancellation)) {
		return;
	}

	if (async_scope_catch(worker, exception)) {
		zend_worker->flags |= ASYNC_COROUTINE_F_EXC_CAUGHT;
		return;
	}

	/* Nobody holds the worker: only the scheduler's reference and finalize's are left, as finalize checks. */
	if (GC_REFCOUNT(&worker->std) <= 2) {
		zend_worker->flags |= ASYNC_COROUTINE_F_EXC_CAUGHT;
		GC_ADDREF(exception);
		async_scheduler_exit_with(exception);
	}
}

/* Gives back the slot a worker took at its spawn; the last one to leave finishes the iterator. */
static void iterator_release_coroutine(async_iterator_t *iterator, async_coroutine_t *worker, const bool is_run)
{
	if (iterator->active_coroutines > 1) {
		iterator->active_coroutines--;
		return;
	}

	iterator->active_coroutines = 0;
	iterator->state = ASYNC_ITERATOR_FINISHED;
	iterator_end_worker(iterator, worker, is_run);
}

/* A worker cancelled before its first run: its slot and its reference go here. After a bailout nothing
 * more runs, and the iterator's exception goes with it. */
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
		async_scope_remove_coroutine(worker);
		zend_hash_index_del(&ASYNC_G(coroutines), worker->std.handle);
		OBJ_RELEASE(&worker->std);
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

	if (iterator->extended_dtor != NULL) {
		const async_iterator_method_t extended_dtor = iterator->extended_dtor;
		iterator->extended_dtor = NULL;
		extended_dtor(iterator);
	}

	if (iterator->hash_iterator != (uint32_t) -1) {
		zend_hash_iterator_del(iterator->hash_iterator);
	}

	zval_ptr_dtor(&iterator->array);

	if (iterator->zend_iterator != NULL) {
		zend_iterator_dtor(iterator->zend_iterator);
		iterator->zend_iterator = NULL;
	}

	if (iterator->fcall != NULL) {
		zend_fcall_t *fcall = iterator->fcall;
		iterator->fcall = NULL;
		ZEND_ASYNC_FCALL_FREE(fcall);
	}

	if (iterator->exception != NULL) {
		zend_object *exception = iterator->exception;
		iterator->exception = NULL;
		OBJ_RELEASE(exception);
	}
}

/* The Traversable is moved by one worker at a time: a move may run PHP code that suspends, and the
 * microtask spawns no worker meanwhile. A microtask the tick dropped while it was cancelled is queued
 * again; the cancel is lifted, or no worker would be spawned after a move that did not suspend. A walk
 * another worker stopped during the move stays stopped: TrueAsync's end of the move started it again. */
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
	(iterator)->state = ASYNC_ITERATOR_FINISHED; \
	(iterator)->microtask.is_cancelled = true;

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

		/* The array may have changed while the previous handler ran. */
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

			if (current != NULL) {
				ZVAL_COPY(&current_item, current);
				current = &current_item;
			}
		}

		if (current == NULL) {
			ITERATOR_FINISH(iterator);
			break;
		}

		/* An undefined slot of an object's properties table is skipped. */
		if (Z_TYPE_P(current) == IS_INDIRECT) {
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

		/* The next element before the call, as foreach does: the handler may change the array. */
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
 * its workers (TrueAsync's async_iterator_apply_exception). A cancellation and an exit stay where
 * they are: they end the worker, not the walk's result. */
static void iterator_take_exception(async_iterator_t *iterator)
{
	zend_object *exception = EG(exception);

	if (EXPECTED(exception == NULL) || instanceof_function(exception->ce, async_ce_cancellation) ||
		async_is_exit_object(exception)) {
		return;
	}

	GC_ADDREF(exception);
	zend_clear_exception();

	if (iterator->exception != NULL) {
		zend_exception_set_previous(exception, iterator->exception);
	}

	iterator->exception = exception;

	async_scope_t *scope = iterator->scope;

	if (scope->event.flags & ASYNC_SCOPE_F_CANCELLED) {
		return;
	}

	async_scope_cancel(scope,
					   async_new_exception(async_ce_cancellation, "Cancellation of the iterator due to an exception"),
					   true,
					   (scope->event.flags & ASYNC_SCOPE_F_DISPOSE_SAFELY) != 0);
}

void async_iterator_run(async_iterator_t *iterator)
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
	async_iterator_run(iterator);
	iterator_release_coroutine(iterator, (async_coroutine_t *) worker, true);
	ZEND_ASYNC_MICROTASK_RELEASE(&iterator->microtask);
}

bool async_iterator_run_in_coroutine(async_iterator_t *iterator)
{
	return iterator_worker_spawn(iterator) != NULL;
}
