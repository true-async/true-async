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
#include "zend_enum.h"
#include "zend_exceptions.h"
#include "zend_interfaces.h"
#include "zend_weakrefs.h"
#include "php_true_async.h"
#include "collector.h"
#include "coroutine.h"
#include "exceptions.h"
#include "pool.h"
#include "scheduler.h"
#include "scope.h"
#include "pool_arginfo.h"

/* Ports TrueAsync's pool (pool.c of ext/async, cited below by line) as the extension's own class: the core
 * has no pool type (dev/plans/S9-pool.md, introduction). A resource counts as active from the moment it
 * leaves the idle buffer, or the factory is called for it, until it is handed out or destroyed, so a hook
 * that suspends never lets another coroutine make one past `max` (TrueAsync's
 * 056-pool_counts_resource_during_hooks). */

zend_class_entry *async_ce_pool = NULL;
zend_class_entry *async_ce_pool_exception = NULL;
zend_class_entry *async_ce_circuit_breaker_state = NULL;
zend_class_entry *async_ce_circuit_breaker_strategy = NULL;

static zend_object_handlers pool_handlers;

static zend_always_inline async_pool_t *pool_from_object(zend_object *object)
{
	return (async_pool_t *) ((char *) object - offsetof(async_pool_t, std));
}

#define THIS_POOL pool_from_object(Z_OBJ_P(ZEND_THIS))

///////////////////////////////////////////////////////////////////
/// Resources and hooks
///////////////////////////////////////////////////////////////////

/* What a hook made of a resource (S9-pool.md section 8, item 12). */
typedef enum
{
	POOL_HOOK_ACCEPTED,
	POOL_HOOK_REJECTED, /* false, or an Exception: the resource is destroyed and nobody hears of it */
	POOL_HOOK_FAILED,   /* an Error, a cancellation or exit(), left pending for the caller */
} pool_hook_verdict_t;

/* Makes a resource into `resource`; false with the factory's exception pending. */
static bool pool_create_resource(async_pool_t *pool, zval *resource)
{
	zend_call_known_fcc(&pool->factory, resource, 0, NULL, NULL);

	/* A factory returning by reference: the pool hands out and counts the value. */
	if (UNEXPECTED(Z_ISREF_P(resource))) {
		zend_unwrap_reference(resource);
	}

	return EXPECTED(EG(exception) == NULL);
}

/* Calls the destructor with `resource` and releases it; the destructor's exception stays pending. Nothing is
 * pending on entry: a call made with an exception pending does not run. */
static void pool_destroy_resource(async_pool_t *pool, zval *resource)
{
	if (ZEND_FCC_INITIALIZED(pool->destructor)) {
		zend_call_known_fcc(&pool->destructor, NULL, 1, resource, NULL);
	}

	zval_ptr_dtor(resource);
}

/* Destroys `resource` after a FAILED verdict, the hook's error kept for the caller. */
static void pool_destroy_after_failure(async_pool_t *pool, zval *resource)
{
	zend_object *const hook_error = EG(exception);

	EG(exception) = NULL;
	pool_destroy_resource(pool, resource);

	zend_object *const destructor_error = EG(exception);

	EG(exception) = NULL;
	async_exception_settle(hook_error, destructor_error);
}

/* Asks `hook` about `resource`; a hook not given accepts. A rejecting Exception goes to `*rejection`, owned
 * by the caller; it stays NULL for any other verdict. */
static pool_hook_verdict_t
pool_check_resource(const zend_fcall_info_cache *hook, zval *resource, zend_object **rejection)
{
	*rejection = NULL;

	if (!ZEND_FCC_INITIALIZED(*hook)) {
		return POOL_HOOK_ACCEPTED;
	}

	zval retval;

	zend_call_known_fcc(hook, &retval, 1, resource, NULL);

	zend_object *const exception = EG(exception);

	if (EXPECTED(exception == NULL)) {
		const zval *answer = &retval;

		ZVAL_DEREF(answer);

		const bool is_rejected = Z_TYPE_P(answer) == IS_FALSE;

		zval_ptr_dtor(&retval);

		return is_rejected ? POOL_HOOK_REJECTED : POOL_HOOK_ACCEPTED;
	}

	if (UNEXPECTED(!instanceof_function(exception->ce, zend_ce_exception))) {
		return POOL_HOOK_FAILED;
	}

	GC_ADDREF(exception);
	zend_clear_exception();
	*rejection = exception;

	return POOL_HOOK_REJECTED;
}

///////////////////////////////////////////////////////////////////
/// Resources handed out
///////////////////////////////////////////////////////////////////

/* release() takes back only what acquire() handed out (S9-pool.md section 8, items 4 and 13): an object
 * counts in `handed_out`, as a factory may return one shared object, a PHP resource in `resources_out`, any
 * other value in `values_out`. */

static void pool_hand_out(async_pool_t *pool, const zval *resource)
{
	if (Z_TYPE_P(resource) == IS_RESOURCE) {
		zval *const count = zend_hash_index_lookup(&pool->resources_out, Z_RES_HANDLE_P(resource));

		if (Z_TYPE_P(count) == IS_NULL) {
			ZVAL_LONG(count, 0);
		}

		Z_LVAL_P(count)++;
		return;
	}

	if (Z_TYPE_P(resource) != IS_OBJECT) {
		pool->values_out++;
		return;
	}

	zval *const count = zend_hash_index_find(&pool->handed_out, zend_object_to_weakref_key(Z_OBJ_P(resource)));

	if (count != NULL) {
		Z_LVAL_P(count)++;
		return;
	}

	zval first;

	ZVAL_LONG(&first, 1);
	zend_weakrefs_hash_add(&pool->handed_out, Z_OBJ_P(resource), &first);
}

/* False when the pool has not handed `resource` out, or has got it back already. */
static bool pool_take_back(async_pool_t *pool, const zval *resource)
{
	if (Z_TYPE_P(resource) == IS_RESOURCE) {
		const zend_ulong handle = Z_RES_HANDLE_P(resource);
		zval *const count = zend_hash_index_find(&pool->resources_out, handle);

		if (UNEXPECTED(count == NULL)) {
			return false;
		}

		if (--Z_LVAL_P(count) == 0) {
			zend_hash_index_del(&pool->resources_out, handle);
		}

		return true;
	}

	if (Z_TYPE_P(resource) != IS_OBJECT) {
		if (UNEXPECTED(pool->values_out == 0)) {
			return false;
		}

		pool->values_out--;
		return true;
	}

	zend_object *const object = Z_OBJ_P(resource);
	zval *const count = zend_hash_index_find(&pool->handed_out, zend_object_to_weakref_key(object));

	if (UNEXPECTED(count == NULL)) {
		return false;
	}

	if (--Z_LVAL_P(count) == 0) {
		zend_weakrefs_hash_del(&pool->handed_out, object);
	}

	return true;
}

///////////////////////////////////////////////////////////////////
/// The POOL wait kind
///////////////////////////////////////////////////////////////////

/* TrueAsync's waiters (pool.c:440-532) on this extension's wait records, as the channel's: a parked acquire() links
 * its coroutine's first waker record with the POOL kind and puts it in `waiters`. A wake promises the waiter a place,
 * an idle resource or room for a new one, which nobody else takes until it runs (S9-pool.md section 4). */

/* A place is promised to the record's coroutine. */
#define POOL_RECORD_F_RESERVED (1u << ASYNC_CALLBACK_F_KIND_SHIFT)

static zend_always_inline async_pool_t *pool_of_record(const async_coroutine_event_callback_t *record)
{
	return (async_pool_t *) ((char *) record->event - offsetof(async_pool_t, base));
}

/* The places open to a caller that holds no promise: idle resources and room, less those promised. idle + active
 * never passes max, so the places are max - active. */
static zend_always_inline uint32_t pool_unreserved_places(const async_pool_t *pool)
{
	ZEND_ASSERT(pool->active + pool->reserved <= pool->max && "a promise is made only for a free place");

	return pool->max - pool->active - pool->reserved;
}

/* Promises a free place to the oldest waiter without one and wakes it; it stays queued until it runs. Called once
 * for each place freed (pool.c:503-520). */
static void pool_wake_waiter(async_pool_t *pool)
{
	if (pool->is_closed || pool_unreserved_places(pool) == 0) {
		return;
	}

	for (uint32_t i = 0; i < pool->waiters.length; i++) {
		async_coroutine_event_callback_t *const record = pool->waiters.records[i];

		if (record->event_callback.flags & POOL_RECORD_F_RESERVED) {
			continue;
		}

		record->event_callback.flags |= POOL_RECORD_F_RESERVED;
		pool->reserved++;
#ifdef TRUE_ASYNC_TEST_HOOKS
		async_collector_check_event_wake(record->coroutine);
#endif
		async_scheduler_enqueue(&record->coroutine->coroutine, NULL, false);
		return;
	}
}

/* A caller leaving with a promised place it did not take hands the place to the next waiter (pool.c:1015-1019). */
static void pool_reservation_drop(async_pool_t *pool, bool *has_reservation)
{
	if (!*has_reservation) {
		return;
	}

	*has_reservation = false;
	pool->reserved--;
	pool_wake_waiter(pool);
}

/* Takes the record out of the queue, unless close() did, and clears its `event`. True when a place was promised to
 * it, which stays counted in `reserved` until the caller takes it or drops it. */
static bool pool_record_leave(async_coroutine_event_callback_t *record)
{
	async_pool_t *const pool = pool_of_record(record);

	async_wait_queue_remove(&pool->waiters, record);
	record->event = NULL;

	return (record->event_callback.flags & POOL_RECORD_F_RESERVED) != 0;
}

/* TrueAsync's line (pool.c:629-639): without `reserved`, idle resources beside parked waiters read as a lost wake. */
static zend_string *pool_record_info(const async_coroutine_event_callback_t *record)
{
	const async_pool_t *const pool = pool_of_record(record);

	return zend_strpprintf(0,
						   "Pool(idle=%u, active=%u, reserved=%u, max=%u)",
						   pool->idle.count,
						   pool->active,
						   pool->reserved,
						   pool->max);
}

/* For a frame that never runs again (a bailout, the request's end): the promise goes with it, and nobody is woken. */
static void pool_record_abort(async_coroutine_event_callback_t *record)
{
	async_pool_t *const pool = pool_of_record(record);

	if (pool_record_leave(record)) {
		pool->reserved--;
	}
}

/* Whoever holds the pool can release, close or free it (S9-pool.md section 6). A running healthcheck holds it in
 * C, which the walk counts as a holder from outside. */
static void pool_record_collector_target(const async_coroutine_event_callback_t *record, async_collector_t *collector)
{
	async_collector_report_target(collector, &pool_of_record(record)->std, false);
}

static const async_wait_kind_t pool_wait_kind = {
	.info = pool_record_info,
	.abort = pool_record_abort,
	.collector_target = pool_record_collector_target,
};

/* Parks the current coroutine until a place is promised to it, close() rejects it, or `deadline`, when given,
 * passes with TimeoutException (pool.c:450-492). True with `*has_reservation` set when it came back without an
 * exception; a promise it got along with an exception goes to the next waiter. */
static bool pool_wait(async_pool_t *pool, const php_deadline *deadline, bool *has_reservation)
{
	async_coroutine_t *const waiter = (async_coroutine_t *) ZEND_ASYNC_CURRENT_COROUTINE;

	if (UNEXPECTED(waiter == NULL)) {
		zend_throw_error(NULL, "There is no coroutine to suspend");
		return false;
	}

	async_wait_end(waiter);
	async_wait_queue_make_room(&pool->waiters);

	if (deadline != NULL && UNEXPECTED(!async_reactor_timeout_link(&waiter->waker.records[1], waiter, *deadline))) {
		return false;
	}

	async_coroutine_event_callback_t *const record = &waiter->waker.records[0];

	async_wait_link_outside(record, waiter, (async_awaitable_t *) &pool->base, &pool_wait_kind);
	async_wait_queue_push(&pool->waiters, record);

	ZEND_ASYNC_SUSPEND();

	*has_reservation = pool_record_leave(record);

	if (EXPECTED(EG(exception) == NULL)) {
		return true;
	}

	pool_reservation_drop(pool, has_reservation);

	return false;
}

/* Each waiter with no promised place leaves with a PoolException of its own (S9-pool.md section 8, item 11); a woken
 * one finds the pool closed when it runs. From the tail, rereading the end at each step, as channel_close(). */
static void pool_reject_waiters(async_pool_t *pool)
{
	uint32_t index = pool->waiters.length;

	while (index > 0) {
		index--;

		if (UNEXPECTED(index >= pool->waiters.length)) {
			index = pool->waiters.length;
			continue;
		}

		async_coroutine_event_callback_t *const record = pool->waiters.records[index];

		if (record->event_callback.flags & POOL_RECORD_F_RESERVED) {
			continue;
		}

		async_wait_queue_remove_at(&pool->waiters, index);
#ifdef TRUE_ASYNC_TEST_HOOKS
		async_collector_check_event_wake(record->coroutine);
#endif
		async_scheduler_enqueue(
				&record->coroutine->coroutine, async_new_exception(async_ce_pool_exception, "Pool is closed"), true);
	}
}

///////////////////////////////////////////////////////////////////
/// The circuit breaker
///////////////////////////////////////////////////////////////////

/* Calls the strategy's reportSuccess($pool), or reportFailure($pool, $error) when `error` is given; its
 * exception stays pending. The call holds the strategy, which setCircuitBreakerStrategy() may replace. */
static void pool_strategy_report(async_pool_t *pool, zend_object *error)
{
	zend_object *const strategy = pool->strategy;

	if (strategy == NULL) {
		return;
	}

	zval pool_zval;

	ZVAL_OBJ(&pool_zval, &pool->std);
	GC_ADDREF(strategy);

	if (error == NULL) {
		zend_call_method_with_1_params(strategy, strategy->ce, NULL, "reportSuccess", NULL, &pool_zval);
	} else {
		zval error_zval;

		ZVAL_OBJ(&error_zval, error);
		zend_call_method_with_2_params(strategy, strategy->ce, NULL, "reportFailure", NULL, &pool_zval, &error_zval);
	}

	OBJ_RELEASE(strategy);
}

/* ACTIVE and RECOVERING serve; INACTIVE serves once the strategy's shouldRecover() says true, which moves the
 * pool to RECOVERING (S9-pool.md section 3). False with the strategy's exception pending, or with none for a
 * refusal. */
static bool pool_circuit_allows(async_pool_t *pool)
{
	zend_object *const strategy = pool->strategy;

	if (EXPECTED(pool->circuit_state != ASYNC_POOL_CIRCUIT_INACTIVE)) {
		return true;
	}

	if (strategy == NULL) {
		return false;
	}

	zval should_recover;

	GC_ADDREF(strategy);
	zend_call_method_with_0_params(strategy, strategy->ce, NULL, "shouldRecover", &should_recover);
	OBJ_RELEASE(strategy);

	if (UNEXPECTED(EG(exception) != NULL)) {
		return false;
	}

	const zval *answer = &should_recover;

	ZVAL_DEREF(answer);

	const bool is_recovering = Z_TYPE_P(answer) == IS_TRUE;

	zval_ptr_dtor(&should_recover);

	/* The call may have suspended while another coroutine moved the state. */
	if (is_recovering && pool->circuit_state == ASYNC_POOL_CIRCUIT_INACTIVE) {
		pool->circuit_state = ASYNC_POOL_CIRCUIT_RECOVERING;
	}

	return pool->circuit_state != ASYNC_POOL_CIRCUIT_INACTIVE;
}

///////////////////////////////////////////////////////////////////
/// Acquire, release, close
///////////////////////////////////////////////////////////////////

typedef enum
{
	POOL_TAKEN,
	POOL_TAKE_FAILED, /* an exception is pending */
	POOL_TAKE_CLOSED,
	POOL_TAKE_UNAVAILABLE, /* the circuit breaker refused */
	POOL_TAKE_FULL,
} pool_take_result_t;

/* acquire() and tryAcquire() up to the wait (TrueAsync's pool.c:929-1007): an idle resource beforeAcquire
 * accepts, else a new one while fewer than `max` exist, from a place promised to the caller
 * (`*has_reservation`) or one promised to nobody. A hook or the factory may suspend, so a pool closed
 * meanwhile destroys what it gave and the caller finds it closed. A place the caller frees and does not
 * try again goes to the next waiter. */
static pool_take_result_t pool_take(async_pool_t *pool, zval *result, bool *has_reservation)
{
	for (;;) {
		if (UNEXPECTED(pool->is_closed)) {
			pool_reservation_drop(pool, has_reservation);
			return POOL_TAKE_CLOSED;
		}

		/* The promise stays counted across shouldRecover(), which may suspend while the pool closes. */
		const bool is_allowed = pool_circuit_allows(pool);

		if (UNEXPECTED(pool->is_closed && EG(exception) == NULL)) {
			pool_reservation_drop(pool, has_reservation);
			return POOL_TAKE_CLOSED;
		}

		if (UNEXPECTED(!is_allowed)) {
			pool_reservation_drop(pool, has_reservation);
			/* A place a rejected resource freed on the round before, which this caller meant to take. */
			pool_wake_waiter(pool);
			return EG(exception) != NULL ? POOL_TAKE_FAILED : POOL_TAKE_UNAVAILABLE;
		}

		if (!*has_reservation && pool_unreserved_places(pool) == 0) {
			return POOL_TAKE_FULL;
		}

		/* Spent where the place is taken, with nothing run in between. */
		if (*has_reservation) {
			*has_reservation = false;
			pool->reserved--;
		}

		if (pool->idle.count > 0) {
			zval resource;
			zend_object *rejection;

			zval_circular_buffer_pop(&pool->idle, &resource);
			pool->active++;

			const pool_hook_verdict_t verdict = pool_check_resource(&pool->before_acquire, &resource, &rejection);

			if (EXPECTED(verdict == POOL_HOOK_ACCEPTED && !pool->is_closed)) {
				pool_hand_out(pool, &resource);
				ZVAL_COPY_VALUE(result, &resource);
				return POOL_TAKEN;
			}

			if (rejection != NULL) {
				OBJ_RELEASE(rejection);
			}

			if (UNEXPECTED(verdict == POOL_HOOK_FAILED)) {
				pool_destroy_after_failure(pool, &resource);
				pool->active--;
				pool_wake_waiter(pool);
				return POOL_TAKE_FAILED;
			}

			pool_destroy_resource(pool, &resource);
			pool->active--;

			if (UNEXPECTED(EG(exception) != NULL)) {
				pool_wake_waiter(pool);
				return POOL_TAKE_FAILED;
			}

			continue;
		}

		ZEND_ASSERT(pool->idle.count + pool->active < pool->max && "a free place without an idle resource is room");

		zval resource;

		pool->active++;

		/* A waiter that parked while the factory ran is woken for the place it frees (047). */
		if (UNEXPECTED(!pool_create_resource(pool, &resource))) {
			pool->active--;
			pool_wake_waiter(pool);
			return POOL_TAKE_FAILED;
		}

		if (EXPECTED(!pool->is_closed)) {
			pool_hand_out(pool, &resource);
			ZVAL_COPY_VALUE(result, &resource);
			return POOL_TAKEN;
		}

		pool_destroy_resource(pool, &resource);
		pool->active--;

		if (UNEXPECTED(EG(exception) != NULL)) {
			return POOL_TAKE_FAILED;
		}
	}
}

/* TrueAsync's pool.c:1085-1118 with the order of S9-pool.md section 2: `active` drops once the resource is
 * idle or destroyed, after the strategy's call, which may suspend. The strategy's exception reaches the
 * caller after the release's own work, merged with a destructor's by async_exception_settle(). */
static void pool_release(async_pool_t *pool, const zval *resource)
{
	zend_object *rejection;
	zval owned;

	/* The pool's own reference: the caller's variable may change while a hook suspends. */
	ZVAL_COPY(&owned, resource);

	const pool_hook_verdict_t verdict = pool_check_resource(&pool->before_release, &owned, &rejection);

	if (UNEXPECTED(verdict == POOL_HOOK_FAILED)) {
		pool_destroy_after_failure(pool, &owned);
		pool->active--;
		pool_wake_waiter(pool);
		return;
	}

	if (verdict == POOL_HOOK_ACCEPTED) {
		pool_strategy_report(pool, NULL);
	} else {
		/* TrueAsync's message for a false (pool.c:410-421); a rejecting Exception is reported itself. */
		zend_object *const failure =
				rejection != NULL ? rejection : async_new_exception(zend_ce_exception, "Resource validation failed");

		pool_strategy_report(pool, failure);
		OBJ_RELEASE(failure);
	}

	zend_object *const strategy_error = EG(exception);

	EG(exception) = NULL;

	if (verdict == POOL_HOOK_ACCEPTED && EXPECTED(!pool->is_closed)) {
		zval_circular_buffer_push(&pool->idle, &owned, pool->max);
		zval_ptr_dtor(&owned);
	} else {
		pool_destroy_resource(pool, &owned);
	}

	zend_object *const destructor_error = EG(exception);

	EG(exception) = NULL;
	pool->active--;
	pool_wake_waiter(pool);
	async_exception_settle(strategy_error, destructor_error);
}

///////////////////////////////////////////////////////////////////
/// The healthcheck
///////////////////////////////////////////////////////////////////

/* S9-pool.md section 5: a soft one-shot timer, which keeps nothing alive, starts a check coroutine, and the check
 * arms it again as it ends, so no tick comes while a check runs. TrueAsync checks inside the timer's callback,
 * where a check that suspends ends the request (section 8, item 2). */

static void pool_healthcheck_timer_drop(async_pool_t *pool)
{
	async_io_event_t *const timer = pool->healthcheck_timer;

	pool->healthcheck_timer = NULL;
	async_io_event_orphan(timer);
	async_callbacks_remove(&timer->base.callbacks, &pool->healthcheck_timer_callback);
	async_io_event_release(timer);
}

static void pool_healthcheck_timer_withdraw(async_pool_t *pool)
{
	if (pool->healthcheck_timer == NULL) {
		return;
	}

	zend_hash_index_del(&ASYNC_G(healthcheck_pools), pool->std.handle);
	pool_healthcheck_timer_drop(pool);
}

/* An exception of the check has no caller: the check's cancellation and an exit end it and stay pending, anything
 * else is printed at the request's end. True when the check ends. */
static bool pool_healthcheck_settle(zend_object *exception)
{
	if (exception == NULL) {
		return false;
	}

	if (UNEXPECTED(instanceof_function(exception->ce, async_ce_cancellation) || async_is_exit_object(exception))) {
		async_exception_merge(exception);
		return true;
	}

	async_unobserved_exception_add(exception);

	return false;
}

/* Checks each idle resource once while a place is promised to nobody, so a place promised to a woken waiter stays
 * its, then makes resources up to `min`. A refill factory's exception is dropped: the next acquire() calls the
 * factory again and throws to a caller who can handle it. */
static void pool_healthcheck_run(async_pool_t *pool)
{
	const zend_coroutine_t *const check = ZEND_ASYNC_CURRENT_COROUTINE;
	uint32_t unchecked = pool->idle.count;

	/* A hook may swallow the check's cancellation (close(), the shutdown): the check stops at it all the same. */
	while (unchecked > 0 && pool->idle.count > 0 && !pool->is_closed && !ZEND_COROUTINE_IS_CANCELLED(check) &&
		   pool_unreserved_places(pool) > 0) {
		zval resource;
		zend_object *rejection;

		unchecked--;
		zval_circular_buffer_pop(&pool->idle, &resource);
		pool->active++;

		const pool_hook_verdict_t verdict = pool_check_resource(&pool->healthcheck, &resource, &rejection);
		zend_object *const hook_error = EG(exception);

		EG(exception) = NULL;

		if (rejection != NULL) {
			OBJ_RELEASE(rejection);
		}

		if (verdict == POOL_HOOK_ACCEPTED && EXPECTED(!pool->is_closed)) {
			zval_circular_buffer_push(&pool->idle, &resource, pool->max);
			zval_ptr_dtor(&resource);
		} else {
			pool_destroy_resource(pool, &resource);
		}

		zend_object *const destructor_error = EG(exception);

		EG(exception) = NULL;
		pool->active--;
		pool_wake_waiter(pool);

		const bool is_ended = pool_healthcheck_settle(hook_error);

		if (UNEXPECTED(pool_healthcheck_settle(destructor_error) || is_ended)) {
			return;
		}
	}

	while (!pool->is_closed && !ZEND_COROUTINE_IS_CANCELLED(check) && pool->idle.count + pool->active < pool->min &&
		   pool_unreserved_places(pool) > 0) {
		zval resource;

		pool->active++;

		if (UNEXPECTED(!pool_create_resource(pool, &resource))) {
			zend_object *const factory_error = EG(exception);

			EG(exception) = NULL;
			pool->active--;
			pool_wake_waiter(pool);

			if (UNEXPECTED(instanceof_function(factory_error->ce, async_ce_cancellation) ||
						   async_is_exit_object(factory_error))) {
				EG(exception) = factory_error;
			} else {
				OBJ_RELEASE(factory_error);
			}

			return;
		}

		if (UNEXPECTED(pool->is_closed)) {
			pool_destroy_resource(pool, &resource);
			pool->active--;

			zend_object *const destructor_error = EG(exception);

			EG(exception) = NULL;
			pool_healthcheck_settle(destructor_error);
			return;
		}

		zval_circular_buffer_push(&pool->idle, &resource, pool->max);
		zval_ptr_dtor(&resource);
		pool->active--;
		pool_wake_waiter(pool);
	}
}

static void pool_healthcheck_tick(async_awaitable_t *target,
								  async_event_callback_t *callback,
								  void *result,
								  zend_object *exception);

/* Nothing once the pool is closed or the graceful shutdown began. `may_throw`: a failed submit leaves its Error
 * pending, else it leaves only no healthcheck. */
static void pool_healthcheck_arm(async_pool_t *pool, const bool may_throw)
{
	if (pool->healthcheck_interval_ms == 0 || !ZEND_FCC_INITIALIZED(pool->healthcheck) || pool->is_closed ||
		UNEXPECTED(!ZEND_ASYNC_IS_ACTIVE || ASYNC_G(graceful_shutdown))) {
		return;
	}

	async_io_event_t *const timer = async_io_event_new();

	php_io_op_timer(&timer->op, async_reactor_deadline_from_ms(pool->healthcheck_interval_ms));
	async_callbacks_reserve(&timer->base.callbacks, 1);
	async_callbacks_push_reserved(&timer->base.callbacks, &pool->healthcheck_timer_callback);
	/* After a fatal error no dtor_obj withdraws the timer, and free_obj comes after the reactor is gone. */
	zend_hash_index_add_new_ptr(&ASYNC_G(healthcheck_pools), pool->std.handle, pool);
	pool->healthcheck_timer = timer;

	const bool is_submitted =
			may_throw ? async_reactor_submit_own(timer) == SUCCESS : async_reactor_try_submit_own(timer) == 0;

	if (UNEXPECTED(!is_submitted)) {
		zend_hash_index_del(&ASYNC_G(healthcheck_pools), pool->std.handle);
		pool->healthcheck_timer = NULL;
		async_callbacks_remove(&timer->base.callbacks, &pool->healthcheck_timer_callback);
		async_io_event_release(timer);
	}
}

/* The check's reference may be the pool's last: the close it makes then runs the destructor callable, whose
 * exception has no caller either and must not become the check's outcome, which the scope's error route takes.
 * `is_unrun`: a check that never ran has no outcome left, and a cancellation there would end the request. */
static void pool_healthcheck_release(zend_object *pool_object, const bool is_unrun)
{
	zend_object *const pending = EG(exception);

	EG(exception) = NULL;
	OBJ_RELEASE(pool_object);

	zend_object *const close_error = EG(exception);

	EG(exception) = pending;

	if (UNEXPECTED(is_unrun && close_error != NULL && instanceof_function(close_error->ce, async_ce_cancellation))) {
		OBJ_RELEASE(close_error);
		return;
	}

	pool_healthcheck_settle(close_error);
}

/* The check holds the pool from its tick to its end, so a free of the pool waits for it. */
static void pool_healthcheck_entry(void)
{
	zend_coroutine_t *const check = ZEND_ASYNC_CURRENT_COROUTINE;
	zend_object *const pool_object = check->extended_data;
	async_pool_t *const pool = pool_from_object(pool_object);

	pool_healthcheck_run(pool);

	pool->healthcheck_coroutine = NULL;
	check->extended_data = NULL;
	pool_healthcheck_arm(pool, false);
	pool_healthcheck_release(pool_object, false);
}

/* A check that never ran (cancelled by close() or the shutdown, or refused) lets the pool go at its free. */
static void pool_healthcheck_dispose(zend_coroutine_t *check)
{
	zend_object *const pool_object = check->extended_data;

	if (pool_object == NULL) {
		return;
	}

	async_pool_t *const pool = pool_from_object(pool_object);

	check->extended_data = NULL;

	if (UNEXPECTED(pool->healthcheck_coroutine == check)) {
		pool->healthcheck_coroutine = NULL;
	}

	pool_healthcheck_release(pool_object, true);
}

/* The timer fired, in scheduler context: one check coroutine in a scope of its own under the global scope, as
 * the task group's reporter (task_group.c, task_group_report()). */
static void
pool_healthcheck_tick(async_awaitable_t *target, async_event_callback_t *callback, void *result, zend_object *exception)
{
	(void) target;
	(void) result;
	(void) exception;

	async_pool_t *const pool =
			(async_pool_t *) ((char *) callback - offsetof(async_pool_t, healthcheck_timer_callback));
	async_scope_t *const global_scope = ASYNC_G(global_scope);

	pool_healthcheck_timer_withdraw(pool);

	if (UNEXPECTED(pool->is_closed || ASYNC_G(graceful_shutdown) || global_scope == NULL)) {
		return;
	}

	async_coroutine_t *const check = async_coroutine_new();

	check->coroutine.internal_entry = pool_healthcheck_entry;
	check->coroutine.extended_data = &pool->std;
	check->coroutine.extended_dispose = pool_healthcheck_dispose;
	GC_ADDREF(&pool->std);
	async_scope_add_coroutine(async_scope_new(global_scope), check);
	pool->healthcheck_coroutine = &check->coroutine;

	if (UNEXPECTED(!async_scheduler_enqueue(&check->coroutine, NULL, false))) {
		async_scope_discard_coroutine(check);
	}
}

static void pool_healthcheck_stop(async_pool_t *pool)
{
	pool_healthcheck_timer_withdraw(pool);

	if (UNEXPECTED(pool->healthcheck_coroutine != NULL)) {
		async_coroutine_cancel((async_coroutine_t *) pool->healthcheck_coroutine, NULL, false, false);
	}
}

void async_pool_request_startup(void)
{
	zend_hash_init(&ASYNC_G(healthcheck_pools), 8, NULL, NULL, false);
}

void async_pool_request_shutdown(void)
{
	async_pool_t *pool;

	ZEND_HASH_FOREACH_PTR(&ASYNC_G(healthcheck_pools), pool)
	{
		pool_healthcheck_timer_drop(pool);
	}
	ZEND_HASH_FOREACH_END();

	zend_hash_destroy(&ASYNC_G(healthcheck_pools));
}

/* TrueAsync's pool.c:1121-1181: stops the healthcheck, rejects the waiters, then destroys every idle resource even
 * when a destructor throws; the exceptions chain through `previous`, the last one on top, and go under an
 * exception in flight when close() started (a pool freed during an unwind), both by async_exception_settle(). Each
 * resource leaves the buffer before its destructor runs. */
static void pool_close(async_pool_t *pool)
{
	if (pool->is_closed) {
		return;
	}

	pool->is_closed = true;

	zend_object *const in_flight = EG(exception);
	zend_object *destructor_errors = NULL;

	EG(exception) = NULL;
	pool_healthcheck_stop(pool);
	pool_reject_waiters(pool);

	while (pool->idle.count > 0) {
		zval resource;

		zval_circular_buffer_pop(&pool->idle, &resource);
		pool->active++;
		pool_destroy_resource(pool, &resource);
		pool->active--;

		zend_object *const destructor_error = EG(exception);

		EG(exception) = NULL;
		async_exception_settle(destructor_error, destructor_errors);
		destructor_errors = EG(exception);
		EG(exception) = NULL;
	}

	async_exception_settle(in_flight, destructor_errors);
}

///////////////////////////////////////////////////////////////////
/// Objects
///////////////////////////////////////////////////////////////////

static zend_object *pool_object_create(zend_class_entry *class_entry)
{
	async_pool_t *const pool = zend_object_alloc(sizeof(async_pool_t), class_entry);

	memset(pool, 0, offsetof(async_pool_t, std));
	async_event_init_in_object(&pool->base, 0, offsetof(async_pool_t, std));
	pool->healthcheck_timer_callback.callback = pool_healthcheck_tick;
	zend_hash_init(&pool->handed_out, 0, NULL, NULL, false);
	zend_hash_init(&pool->resources_out, 0, NULL, NULL, false);
	zend_object_std_init(&pool->std, class_entry);
	object_properties_init(&pool->std, class_entry);

	return &pool->std;
}

/* The destructor closes the pool, not free_obj: PHP's GC calls dtor_obj while every object of the garbage is
 * intact, so a destructor callable that captures an object of the cycle still finds it (S9-pool.md
 * section 8, item 8). */
static void pool_object_destroy(zend_object *object)
{
	pool_close(pool_from_object(object));
}

static void pool_callable_release(zend_fcall_info_cache *callable)
{
	if (ZEND_FCC_INITIALIZED(*callable)) {
		zend_fcc_dtor(callable);
	}
}

/* After a fatal error no destructor ran: the idle resources are released without the destructor callable. */
static void pool_object_free(zend_object *object)
{
	async_pool_t *const pool = pool_from_object(object);

	pool_healthcheck_timer_withdraw(pool);
	async_wait_queue_free(&pool->waiters);
	zend_object_std_dtor(object);
	zval_circular_buffer_dtor(&pool->idle);
	pool_callable_release(&pool->factory);
	pool_callable_release(&pool->destructor);
	pool_callable_release(&pool->healthcheck);
	pool_callable_release(&pool->before_acquire);
	pool_callable_release(&pool->before_release);

	if (pool->strategy != NULL) {
		OBJ_RELEASE(pool->strategy);
	}

	zend_weakrefs_hash_destroy(&pool->handed_out);
	zend_hash_destroy(&pool->resources_out);
}

static void pool_callable_gc(zend_get_gc_buffer *gc_buffer, zend_fcall_info_cache *callable)
{
	if (ZEND_FCC_INITIALIZED(*callable)) {
		zend_get_gc_buffer_add_fcc(gc_buffer, callable);
	}
}

/* The idle resources, the callables and the strategy (TrueAsync's pool.c:1283-1338); a resource handed out
 * is its holder's. */
static HashTable *pool_object_gc(zend_object *object, zval **table, int *num)
{
	async_pool_t *const pool = pool_from_object(object);
	zend_get_gc_buffer *const gc_buffer = zend_get_gc_buffer_create();

	for (uint32_t i = 0; i < pool->idle.count; i++) {
		zend_get_gc_buffer_add_zval(gc_buffer, zval_circular_buffer_at(&pool->idle, i));
	}

	pool_callable_gc(gc_buffer, &pool->factory);
	pool_callable_gc(gc_buffer, &pool->destructor);
	pool_callable_gc(gc_buffer, &pool->healthcheck);
	pool_callable_gc(gc_buffer, &pool->before_acquire);
	pool_callable_gc(gc_buffer, &pool->before_release);

	if (pool->strategy != NULL) {
		zend_get_gc_buffer_add_obj(gc_buffer, pool->strategy);
	}

	zend_get_gc_buffer_use(gc_buffer, table, num);

	return NULL;
}

///////////////////////////////////////////////////////////////////
/// Methods
///////////////////////////////////////////////////////////////////

/* A count above UINT32_MAX is no limit any pool reaches. */
static zend_always_inline uint32_t pool_count_from_long(const zend_long count)
{
	return (zend_ulong) count > UINT32_MAX ? UINT32_MAX : (uint32_t) count;
}

ZEND_METHOD(Async_Pool, __construct)
{
	zend_fcall_info fci;
	zend_fcall_info_cache callables[5] = { empty_fcall_info_cache,
										   empty_fcall_info_cache,
										   empty_fcall_info_cache,
										   empty_fcall_info_cache,
										   empty_fcall_info_cache };
	zend_long min = 0;
	zend_long max = 10;
	zend_long healthcheck_interval = 0;

	ZEND_PARSE_PARAMETERS_START(1, 8)
		Z_PARAM_FUNC_NO_TRAMPOLINE_FREE(fci, callables[0])
		Z_PARAM_OPTIONAL
		Z_PARAM_FUNC_NO_TRAMPOLINE_FREE_OR_NULL(fci, callables[1])
		Z_PARAM_FUNC_NO_TRAMPOLINE_FREE_OR_NULL(fci, callables[2])
		Z_PARAM_FUNC_NO_TRAMPOLINE_FREE_OR_NULL(fci, callables[3])
		Z_PARAM_FUNC_NO_TRAMPOLINE_FREE_OR_NULL(fci, callables[4])
		Z_PARAM_LONG(min)
		Z_PARAM_LONG(max)
		Z_PARAM_LONG(healthcheck_interval)
	ZEND_PARSE_PARAMETERS_END_EX(goto release_callables);

	async_pool_t *const pool = THIS_POOL;

	/* TrueAsync's guard (pool.c:1366-1372): a second construction would orphan the resources made. */
	if (UNEXPECTED(pool->is_constructed)) {
		zend_throw_exception(async_ce_pool_exception, "Pool is already initialized", 0);
		goto release_callables;
	}

	if (UNEXPECTED(min < 0)) {
		zend_argument_value_error(6, "must be >= 0");
		goto release_callables;
	}

	if (UNEXPECTED(max < 1)) {
		zend_argument_value_error(7, "must be >= 1");
		goto release_callables;
	}

	if (UNEXPECTED(min > max)) {
		zend_argument_value_error(6, "must be <= max");
		goto release_callables;
	}

	if (UNEXPECTED(healthcheck_interval < 0)) {
		zend_argument_value_error(8, "must be >= 0");
		goto release_callables;
	}

	zend_fcc_dup(&pool->factory, &callables[0]);

	if (ZEND_FCC_INITIALIZED(callables[1])) {
		zend_fcc_dup(&pool->destructor, &callables[1]);
	}

	if (ZEND_FCC_INITIALIZED(callables[2])) {
		zend_fcc_dup(&pool->healthcheck, &callables[2]);
	}

	if (ZEND_FCC_INITIALIZED(callables[3])) {
		zend_fcc_dup(&pool->before_acquire, &callables[3]);
	}

	if (ZEND_FCC_INITIALIZED(callables[4])) {
		zend_fcc_dup(&pool->before_release, &callables[4]);
	}

	pool->min = pool_count_from_long(min);
	pool->max = pool_count_from_long(max);
	pool->healthcheck_interval_ms = healthcheck_interval;
	pool->is_constructed = true;

	/* The factory may suspend: the resource being made counts as active, as in acquire(). It reaches the pool
	 * through debug_backtrace(), so it may acquire from it or close it: the loop stops as the healthcheck's refill
	 * does, and calls the factory at most `min` times, as TrueAsync does. */
	for (uint32_t i = 0; i < pool->min; i++) {
		if (UNEXPECTED(pool->is_closed || pool->idle.count + pool->active >= pool->min ||
					   pool_unreserved_places(pool) == 0)) {
			break;
		}

		zval resource;

		pool->active++;

		if (UNEXPECTED(!pool_create_resource(pool, &resource))) {
			pool->active--;
			/* TrueAsync stops at the first exception (pool.c:855-864) and leaves the resources made to the
			 * free; here they are destroyed now, with the factory's exception kept. */
			pool_close(pool);
			break;
		}

		if (UNEXPECTED(pool->is_closed)) {
			pool_destroy_resource(pool, &resource);
			pool->active--;
			break;
		}

		zval_circular_buffer_push(&pool->idle, &resource, pool->max);
		zval_ptr_dtor(&resource);
		pool->active--;
		/* A waiter that parked while the factory ran is woken for the resource, as in acquire(). */
		pool_wake_waiter(pool);
	}

	pool_healthcheck_arm(pool, true);

	return;

	/* A trampoline the parser copied belongs to the pool once duplicated, else it is freed here. */
release_callables:
	for (uint32_t i = 0; i < sizeof(callables) / sizeof(callables[0]); i++) {
		if (ZEND_FCC_INITIALIZED(callables[i])) {
			zend_release_fcall_info_cache(&callables[i]);
		}
	}
}

ZEND_METHOD(Async_Pool, acquire)
{
	zend_long timeout = 0;

	ZEND_PARSE_PARAMETERS_START(0, 1)
		Z_PARAM_OPTIONAL
		Z_PARAM_LONG(timeout)
	ZEND_PARSE_PARAMETERS_END();

	THROW_IF_UNAVAILABLE();

	async_pool_t *const pool = THIS_POOL;
	bool has_reservation = false;
	/* One deadline for every wait of the call, from the first: a wake that leads to no resource waits on. */
	php_deadline deadline;
	bool has_deadline = false;

	for (;;) {
		switch (pool_take(pool, return_value, &has_reservation)) {
			case POOL_TAKEN:
				return;
			case POOL_TAKE_FAILED:
				RETURN_THROWS();
			case POOL_TAKE_CLOSED:
				zend_throw_exception(async_ce_pool_exception, "Pool is closed", 0);
				RETURN_THROWS();
			case POOL_TAKE_UNAVAILABLE:
				zend_throw_exception(
						async_ce_service_unavailable_exception, "Service is unavailable (circuit breaker is open)", 0);
				RETURN_THROWS();
			case POOL_TAKE_FULL:
				break;
		}

		if (timeout > 0 && !has_deadline) {
			deadline = async_reactor_deadline_from_ms(timeout);
			has_deadline = true;
		}

		if (UNEXPECTED(!pool_wait(pool, has_deadline ? &deadline : NULL, &has_reservation))) {
			RETURN_THROWS();
		}
	}
}

ZEND_METHOD(Async_Pool, tryAcquire)
{
	ZEND_PARSE_PARAMETERS_NONE();

	bool has_reservation = false;

	switch (pool_take(THIS_POOL, return_value, &has_reservation)) {
		case POOL_TAKEN:
			return;
		case POOL_TAKE_FAILED:
			RETURN_THROWS();
		default:
			RETURN_NULL();
	}
}

ZEND_METHOD(Async_Pool, release)
{
	zval *resource;

	ZEND_PARSE_PARAMETERS_START(1, 1)
		Z_PARAM_ZVAL(resource)
	ZEND_PARSE_PARAMETERS_END();

	async_pool_t *const pool = THIS_POOL;

	/* Before beforeRelease, which may suspend: a second release of the object during it must not pass. */
	if (UNEXPECTED(!pool_take_back(pool, resource))) {
		zend_throw_exception(
				async_ce_pool_exception, "The resource was not acquired from this pool, or was released already", 0);
		RETURN_THROWS();
	}

	pool_release(pool, resource);
}

ZEND_METHOD(Async_Pool, close)
{
	ZEND_PARSE_PARAMETERS_NONE();

	pool_close(THIS_POOL);
}

ZEND_METHOD(Async_Pool, isClosed)
{
	ZEND_PARSE_PARAMETERS_NONE();

	RETURN_BOOL(THIS_POOL->is_closed);
}

ZEND_METHOD(Async_Pool, count)
{
	ZEND_PARSE_PARAMETERS_NONE();

	const async_pool_t *const pool = THIS_POOL;

	RETURN_LONG(pool->idle.count + pool->active);
}

ZEND_METHOD(Async_Pool, idleCount)
{
	ZEND_PARSE_PARAMETERS_NONE();

	RETURN_LONG(THIS_POOL->idle.count);
}

ZEND_METHOD(Async_Pool, activeCount)
{
	ZEND_PARSE_PARAMETERS_NONE();

	RETURN_LONG(THIS_POOL->active);
}

/* The old strategy is released last: its destructor may run. */
ZEND_METHOD(Async_Pool, setCircuitBreakerStrategy)
{
	zend_object *strategy = NULL;

	ZEND_PARSE_PARAMETERS_START(1, 1)
		Z_PARAM_OBJ_OF_CLASS_OR_NULL(strategy, async_ce_circuit_breaker_strategy)
	ZEND_PARSE_PARAMETERS_END();

	async_pool_t *const pool = THIS_POOL;
	zend_object *const old_strategy = pool->strategy;

	if (strategy != NULL) {
		GC_ADDREF(strategy);
	}

	pool->strategy = strategy;

	if (old_strategy != NULL) {
		OBJ_RELEASE(old_strategy);
	}
}

static const char *const pool_circuit_case_names[] = {
	[ASYNC_POOL_CIRCUIT_ACTIVE] = "ACTIVE",
	[ASYNC_POOL_CIRCUIT_INACTIVE] = "INACTIVE",
	[ASYNC_POOL_CIRCUIT_RECOVERING] = "RECOVERING",
};

ZEND_METHOD(Async_Pool, getState)
{
	ZEND_PARSE_PARAMETERS_NONE();

	RETURN_OBJ_COPY(
			zend_enum_get_case_cstr(async_ce_circuit_breaker_state, pool_circuit_case_names[THIS_POOL->circuit_state]));
}

ZEND_METHOD(Async_Pool, activate)
{
	ZEND_PARSE_PARAMETERS_NONE();

	THIS_POOL->circuit_state = ASYNC_POOL_CIRCUIT_ACTIVE;
}

ZEND_METHOD(Async_Pool, deactivate)
{
	ZEND_PARSE_PARAMETERS_NONE();

	THIS_POOL->circuit_state = ASYNC_POOL_CIRCUIT_INACTIVE;
}

ZEND_METHOD(Async_Pool, recover)
{
	ZEND_PARSE_PARAMETERS_NONE();

	THIS_POOL->circuit_state = ASYNC_POOL_CIRCUIT_RECOVERING;
}

void async_register_pool_ce(zend_class_entry *circuit_breaker)
{
	async_ce_pool_exception = register_class_Async_PoolException(async_ce_async_exception);
	async_ce_pool = register_class_Async_Pool(zend_ce_countable, circuit_breaker);
	async_ce_pool->create_object = pool_object_create;
	async_ce_pool->default_object_handlers = &pool_handlers;

	memcpy(&pool_handlers, zend_get_std_object_handlers(), sizeof(zend_object_handlers));
	pool_handlers.offset = offsetof(async_pool_t, std);
	pool_handlers.dtor_obj = pool_object_destroy;
	pool_handlers.free_obj = pool_object_free;
	pool_handlers.get_gc = pool_object_gc;
	pool_handlers.clone_obj = NULL;
}
