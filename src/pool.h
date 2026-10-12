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
	zend_long healthcheck_interval_ms; /* 0: no healthcheck */
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

#endif /* TRUE_ASYNC_POOL_H */
