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
#ifndef PHP_TRUE_ASYNC_H
#define PHP_TRUE_ASYNC_H

/* Kept at the repository root: a static build of php-src finds the module entry by grepping the
 * *.h files of ext/<name> for "phpext_", without descending into subdirectories. */

extern zend_module_entry true_async_module_entry;
#define phpext_true_async_ptr &true_async_module_entry

#define PHP_TRUE_ASYNC_VERSION "0.1.0-dev"

#include "src/true_async_API.h"
#include "src/internal/circular_buffer.h"
#include "src/reactor.h"
#include "src/io_provider.h"
#ifdef TRUE_ASYNC_FUZZ
#include "src/internal/fuzz.h"
#endif

ZEND_BEGIN_MODULE_GLOBALS(true_async)
	circular_buffer_t run_queue;            /* QUEUED coroutines, one entry each; borrowed pointers */
	circular_buffer_t fiber_context_pool;   /* contexts parked with nothing to run */
	circular_buffer_t microtasks;           /* deferred through the defer slot; one reference each, the tick's */
	HashTable coroutines;                   /* enqueued, unfinished coroutines by object handle; borrowed */
	HashTable unobserved_exceptions;        /* by object handle, printed at the request's end; one reference each */
	async_coroutine_t *scheduler_coroutine; /* runs the loop on its own fiber; NULL until work needs it */
	async_reactor_t reactor;                /* the thread's IO queue and the waits on it */
	async_wake_pair_t wake_pair;            /* the reactor's wake descriptors: the thread's, not a request's */
	async_io_provider_t io_provider;        /* registered from the first trigger to RSHUTDOWN */
	async_coroutine_t *interrupt_coroutine; /* runs the VM interrupt for an idle scheduler; NULL when none is alive */
	async_io_event_t *exit_deadline;        /* D16's Timer while armed, once per drain; the scheduler's reference */
	uint32_t last_handler_id;               /* the id of the newest finish or switch handler; 0 is never handed out */
	bool graceful_shutdown;                 /* the graceful shutdown started: once per request */
	bool debug_deadlock;                    /* true_async.debug_deadlock: the deadlock report lists every coroutine */
	uint8_t partial_deadlock;               /* true_async.partial_deadlock: an async_partial_deadlock_t */
	uint8_t collector_backoff;              /* the automatic collector waits the interval << this (collector.c) */
	zend_long partial_deadlock_interval;    /* true_async.partial_deadlock_interval, ms */
	uint64_t collector_last_run;            /* zend_hrtime() of the last automatic run; 0 before the first idle */
#ifdef TRUE_ASYNC_TEST_HOOKS
	uint8_t fault_site;              /* the armed async_test_fault_site_t; ASYNC_TEST_FAULT_NONE when unarmed */
	uint32_t test_block_releases;    /* test wait blocks released in this request */
	uint32_t test_typed_unlinks;     /* records unlinked through a test kind's unlink in this request */
	uint32_t test_aborts;            /* test kind aborts in this request */
	zend_long test_exit_deadline_ms; /* D16's deadline in this request; 0 for ASYNC_EXIT_DEADLINE_MS */
	async_trigger_t *test_trigger;   /* TrueAsync\Test\trigger_new()'s; NULL without */
	void *test_firer;                /* the thread trigger_fire() started; NULL when joined */
#endif
#ifdef TRUE_ASYNC_FUZZ
	async_fuzz_state_t fuzz; /* TRUE_ASYNC_SCHED of this request */
#endif
ZEND_END_MODULE_GLOBALS(true_async)

ZEND_EXTERN_MODULE_GLOBALS(true_async)

/* Implemented only by the classes of this extension: a Coroutine or a Future (src/true_async.c). */
extern zend_class_entry *async_ce_awaitable;
extern zend_class_entry *async_ce_completable;
#define ASYNC_G(v) ZEND_MODULE_GLOBALS_ACCESSOR(true_async, v)

/* Refuses while no scheduler runs (php -r launches none; after the request's last drain the core
 * turns async off), as TrueAsync. */
#define THROW_IF_ASYNC_OFF() \
	do { \
		if (UNEXPECTED(!ZEND_ASYNC_IS_ACTIVE)) { \
			zend_throw_error(NULL, "The operation cannot be executed while async is off"); \
			RETURN_THROWS(); \
		} \
	} while (0)

#define THROW_IF_SCHEDULER_CONTEXT() \
	do { \
		if (UNEXPECTED(ZEND_ASYNC_IN_SCHEDULER_CONTEXT)) { \
			zend_throw_error(NULL, "The operation cannot be executed in the scheduler context"); \
			RETURN_THROWS(); \
		} \
	} while (0)

#define THROW_IF_UNAVAILABLE() \
	do { \
		THROW_IF_ASYNC_OFF(); \
		THROW_IF_SCHEDULER_CONTEXT(); \
	} while (0)

#if defined(ZTS) && defined(COMPILE_DL_TRUE_ASYNC)
ZEND_TSRMLS_CACHE_EXTERN()
#endif

#endif /* PHP_TRUE_ASYNC_H */
