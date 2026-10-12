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
#ifndef TRUE_ASYNC_POOL_H
#define TRUE_ASYNC_POOL_H

#include "php.h"
#include "internal/zval_circular_buffer.h"
#include "true_async_API.h"
#include "reactor.h"

/* Async\Pool (dev/plans/S9-pool.md): reusable resources that coroutines take with acquire() and give back
 * with release(), at most `max` of them alive, with TrueAsync's hooks and circuit breaker. */

/* The cases of Async\CircuitBreakerState. */
typedef enum
{
	ASYNC_POOL_CIRCUIT_ACTIVE,
	ASYNC_POOL_CIRCUIT_INACTIVE,
	ASYNC_POOL_CIRCUIT_RECOVERING,
} async_pool_circuit_state_t;

typedef struct
{
	async_event_t base;          /* the target of the POOL records; no subscribers */
	zval_circular_buffer_t idle; /* the idle resources, oldest first */
	/* The callables; all but the factory uninitialized when not given. */
	zend_fcall_info_cache factory;
	zend_fcall_info_cache destructor;
	zend_fcall_info_cache healthcheck;
	zend_fcall_info_cache before_acquire;
	zend_fcall_info_cache before_release;
	zend_object *strategy; /* the CircuitBreakerStrategy, held; NULL when none is set */
	/* How many times each object resource is handed out and not yet released, by a weak key: an object its
	 * holder dropped leaves the table, and a new object at its address is not taken for it. */
	HashTable handed_out;
	HashTable resources_out; /* the same count for PHP resources, by handle: a request never reuses one */
	uint32_t min;
	uint32_t max;
	uint32_t active;     /* handed out or inside a hook or the factory: count() is idle + active, at most max */
	uint32_t values_out; /* values other than objects handed out to callers and not yet released */
	/* Coroutines parked in acquire(), in arrival order: the POOL records of their wakers. */
	async_wait_queue_t waiters;
	uint32_t reserved; /* places promised to woken waiters that have not run yet */
	/* The soft one-shot Timer op of the next healthcheck while armed; one reference. */
	async_io_event_t *healthcheck_timer;
	async_event_callback_t healthcheck_timer_callback; /* in `healthcheck_timer`'s vector while armed */
	zend_coroutine_t *healthcheck_coroutine;           /* the running check; NULL when none */
	zend_long healthcheck_interval_ms;                 /* 0: no healthcheck */
	async_pool_circuit_state_t circuit_state;
	bool is_constructed; /* a second __construct() throws */
	bool is_closed;
	zend_object std;
} async_pool_t;

extern zend_class_entry *async_ce_pool;
extern zend_class_entry *async_ce_pool_exception;
/* Registered with true_async.stub.php's types; the pool reads them. */
extern zend_class_entry *async_ce_circuit_breaker_state;
extern zend_class_entry *async_ce_circuit_breaker_strategy;

void async_register_pool_ce(zend_class_entry *circuit_breaker);
void async_pool_request_startup(void);
void async_pool_request_shutdown(void);

#endif /* TRUE_ASYNC_POOL_H */
