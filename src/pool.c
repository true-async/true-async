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
#include "coroutine.h"
#include "exceptions.h"
#include "pool.h"
#include "scheduler.h"
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

/* Puts `under`, an exception set aside, beneath the pending one; either may be NULL. An exit wins and drops
 * the other, as it takes no previous. A pending exception held elsewhere too (a scope cancel thrown into
 * each coroutine of the scope, a Future's stored error) is left unchanged, and `under` goes unobserved. */
static void pool_exception_merge(zend_object *under)
{
	zend_object *const pending = EG(exception);

	if (under == NULL) {
		return;
	}

	if (pending == NULL) {
		EG(exception) = under;
		return;
	}

	if (UNEXPECTED(async_is_exit_object(pending))) {
		OBJ_RELEASE(under);
		return;
	}

	if (UNEXPECTED(async_is_exit_object(under))) {
		EG(exception) = under;
		OBJ_RELEASE(pending);
		return;
	}

	if (UNEXPECTED(GC_REFCOUNT(pending) > 1)) {
		async_unobserved_exception_add(under);
		return;
	}

	zend_exception_set_previous(pending, under);
}

/* Destroys `resource` after a FAILED verdict: the hook's error stays on top for the caller, the destructor's
 * exception beneath it, unless the destructor was cancelled, as the caller's cancellation goes on top. */
static void pool_destroy_after_failure(async_pool_t *pool, zval *resource)
{
	zend_object *const hook_error = EG(exception);

	EG(exception) = NULL;
	pool_destroy_resource(pool, resource);

	zend_object *const destructor_error = EG(exception);

	if (UNEXPECTED(destructor_error != NULL && instanceof_function(destructor_error->ce, async_ce_cancellation) &&
				   !instanceof_function(hook_error->ce, async_ce_cancellation))) {
		pool_exception_merge(hook_error);
		return;
	}

	EG(exception) = hook_error;
	pool_exception_merge(destructor_error);
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
 * accepts, else a new one while fewer than `max` exist. A hook or the factory may suspend, so a pool closed
 * meanwhile destroys what it gave and the caller finds it closed. */
static pool_take_result_t pool_take(async_pool_t *pool, zval *result)
{
	for (;;) {
		if (UNEXPECTED(pool->is_closed)) {
			return POOL_TAKE_CLOSED;
		}

		if (UNEXPECTED(!pool_circuit_allows(pool))) {
			return EG(exception) != NULL ? POOL_TAKE_FAILED : POOL_TAKE_UNAVAILABLE;
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
				return POOL_TAKE_FAILED;
			}

			pool_destroy_resource(pool, &resource);
			pool->active--;

			if (UNEXPECTED(EG(exception) != NULL)) {
				return POOL_TAKE_FAILED;
			}

			continue;
		}

		if (pool->idle.count + pool->active >= pool->max) {
			return POOL_TAKE_FULL;
		}

		zval resource;

		pool->active++;

		if (UNEXPECTED(!pool_create_resource(pool, &resource))) {
			pool->active--;
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
 * caller after the release's own work, with a destructor's exception over it. */
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

	pool->active--;
	pool_exception_merge(strategy_error);
}

/* TrueAsync's pool.c:1121-1181: destroys every idle resource even when a destructor throws; the exceptions
 * chain through `previous`, the last one on top, over an exception in flight when close() started (a pool
 * freed during an unwind). Each resource leaves the buffer before its destructor runs. */
static void pool_close(async_pool_t *pool)
{
	if (pool->is_closed) {
		return;
	}

	pool->is_closed = true;

	zend_object *const in_flight = EG(exception);
	zend_object *destructor_errors = NULL;

	EG(exception) = NULL;

	while (pool->idle.count > 0) {
		zval resource;

		zval_circular_buffer_pop(&pool->idle, &resource);
		pool->active++;
		pool_destroy_resource(pool, &resource);
		pool->active--;
		pool_exception_merge(destructor_errors);
		destructor_errors = EG(exception);
		EG(exception) = NULL;
	}

	EG(exception) = destructor_errors;
	pool_exception_merge(in_flight);
}

///////////////////////////////////////////////////////////////////
/// Objects
///////////////////////////////////////////////////////////////////

static zend_object *pool_object_create(zend_class_entry *class_entry)
{
	async_pool_t *const pool = zend_object_alloc(sizeof(async_pool_t), class_entry);

	memset(pool, 0, offsetof(async_pool_t, std));
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

	/* The factory may suspend: the resource being made counts as active, as in acquire(). */
	for (uint32_t i = 0; i < pool->min; i++) {
		zval resource;

		pool->active++;

		const bool is_created = pool_create_resource(pool, &resource);

		pool->active--;

		if (UNEXPECTED(!is_created)) {
			/* TrueAsync stops at the first exception (pool.c:854-864) and leaves the resources made to the
			 * free; here they are destroyed now, with the factory's exception kept. */
			pool_close(pool);
			break;
		}

		zval_circular_buffer_push(&pool->idle, &resource, pool->max);
		zval_ptr_dtor(&resource);
	}

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

	(void) timeout;
	THROW_IF_UNAVAILABLE();

	switch (pool_take(THIS_POOL, return_value)) {
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
			zend_throw_error(NULL, "Waiting in Async\\Pool::acquire() is not implemented yet");
			RETURN_THROWS();
	}
}

ZEND_METHOD(Async_Pool, tryAcquire)
{
	ZEND_PARSE_PARAMETERS_NONE();

	switch (pool_take(THIS_POOL, return_value)) {
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
