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
#ifndef TRUE_ASYNC_SCHEDULER_H
#define TRUE_ASYNC_SCHEDULER_H

#include "php.h"
#include "Zend/zend_fibers.h"
#include "coroutine.h"

/* A fiber context a coroutine runs on (dev/plans/S3.md, section 5). A context outlives its
 * coroutine: when the coroutine finishes, the context's loop runs the next one, or parks in the
 * pool. Main's context is a copy of the engine's own and never enters the pool. */
struct _async_fiber_context_s
{
	zend_fiber_context context;
	zend_execute_data *execute_data; /* the parked frame, stored by suspend(); stale while it runs */
};

/* Registers the scheduler slots with the core; MINIT, once the extension is enabled. False when the
 * core refused (it warned why): the extension stays loaded and inert. */
bool async_scheduler_register(void);

/* Per-request state: the run queue, the context pool, the coroutine registry. */
void async_scheduler_request_startup(void);
void async_scheduler_request_shutdown(void);

/* A new coroutine in CREATED with no entry point, for the caller to enqueue; the scheduler holds its
 * birth reference until it finishes. */
async_coroutine_t *async_coroutine_new(void);

/* The enqueue slot (S3.md 4.3): CREATED or SUSPENDED to QUEUED; the running current coroutine with
 * no error goes to the back of the queue (a yield). False with an exception on refusal; a
 * transferred `error` is then released. */
bool async_scheduler_enqueue(zend_coroutine_t *coroutine, zend_object *error, bool transfer_error);

/* Requests the cancellation of `coroutine` (S3.md section 6, TrueAsync's async_coroutine_cancel,
 * coroutine.c:871-1004). `error`, or a new AsyncCancellation("Coroutine cancelled") when it is NULL,
 * is thrown inside the coroutine's suspend() when it next runs; one that never ran finishes without
 * running its body. Inside protect() the first request waits for protect() to return. The running
 * coroutine is not interrupted: the error becomes its outcome. A finished coroutine ignores it. A
 * transferred `error` is the callee's. False with an exception when the coroutine cannot be queued. */
bool async_coroutine_cancel(async_coroutine_t *coroutine, zend_object *error, bool transfer_error);

/* Starts the graceful shutdown (S3.md section 6, TrueAsync's start_graceful_shutdown_with,
 * scheduler.c:1005-1030): every unfinished coroutine is cancelled with `cancellation` (borrowed), or
 * with AsyncCancellation("Graceful shutdown") when it is NULL, protection cleared, so its try/finally
 * blocks run. Once per request: a later call does nothing. */
void async_scheduler_graceful_shutdown(zend_object *cancellation);

/* What ends the request cancels the coroutines: the graceful shutdown starts, or, once it runs,
 * every unfinished coroutine is cancelled again, what was spawned since included. exit() calls it
 * directly. */
void async_scheduler_cancel_for_exit(void);

/* Ends the request on an exception nothing can catch (an unobserved outcome, a microtask's): it
 * joins the exit exception and the coroutines are cancelled as above. Takes the reference. */
void async_scheduler_exit_with(zend_object *exception);

/* Parks the current coroutine until `target` finishes (S3.md 4.1); the caller holds a reference to
 * `target` and reads the outcome from it. Never called in scheduler context. True once the target
 * finished; false with an exception when there is no running current coroutine, on a self-await, or when
 * the wait is aborted (a cancellation of the waiter). */
bool async_await_coroutine(async_coroutine_t *target);

#endif /* TRUE_ASYNC_SCHEDULER_H */
