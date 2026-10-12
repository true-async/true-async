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
#ifndef TRUE_ASYNC_TEST_HOOKS_H
#define TRUE_ASYNC_TEST_HOOKS_H

#include "php.h"

/* Registered at MINIT in a build with --enable-true-async-test-hooks. */
extern const zend_function_entry true_async_test_hooks_functions[];

/* Registers TrueAsync\Test\Event at MINIT, beside the functions. */
void async_test_hooks_register_classes(void);

/* Joins the thread TrueAsync\Test\trigger_fire() started and frees the test trigger, after the
 * scheduler's last drain. */
void async_test_hooks_request_shutdown(void);

/* Where TrueAsync\Test\fail_at() makes the scheduler fail: a fatal error there, as running out of
 * memory raises, while a wait is half made (dev/plans/S3.md 4.4, the unlink sites U1-U6). A new
 * site also goes into fault_site_names and the message of fail_at() (test_hooks.c). */
typedef enum
{
	ASYNC_TEST_FAULT_NONE = 0,
	ASYNC_TEST_FAULT_ENQUEUE, /* async_scheduler_enqueue, before the push and the unlink of a woken waiter */
	ASYNC_TEST_FAULT_RESERVE, /* async_await_coroutine, the reservation before the record links */
	ASYNC_TEST_FAULT_LINK,    /* async_await_coroutine, the record linked, before the suspend */
} async_test_fault_site_t;

/* Where TrueAsync\Test\fail_io() makes the reactor's queue fail once (every wait for `wait_always`), as the OS
 * or the queue would: the call returns its error with errno EIO (EMFILE for the wake pair) and the request
 * goes on. A new site also goes into io_fault_site_names and the message of fail_io() (test_hooks.c). */
typedef enum
{
	ASYNC_TEST_IO_FAULT_QUEUE_CREATE = 1 << 0,     /* reactor_queue: neither queue can be created */
	ASYNC_TEST_IO_FAULT_SUBMIT = 1 << 1,           /* queue_push: the queue refuses a submit */
	ASYNC_TEST_IO_FAULT_WAIT = 1 << 2,             /* reactor_poll: the queue's wait fails */
	ASYNC_TEST_IO_FAULT_WAKE_PAIR = 1 << 3,        /* wake_pair_open: the descriptors cannot be made */
	ASYNC_TEST_IO_FAULT_COMPLETE = 1 << 4,         /* reactor_dispatch: the next completion ends with EIO */
	ASYNC_TEST_IO_FAULT_ANSWER_IN_FLIGHT = 1 << 5, /* io_wait_deliver: the answer leaves the op in flight, as the
													* Ring's early Timeout does */
	ASYNC_TEST_IO_FAULT_WAIT_ALWAYS = 1 << 6,      /* reactor_poll: every wait fails, as a broken queue's: never
													* disarmed */
} async_test_io_fault_t;

#ifdef TRUE_ASYNC_TEST_HOOKS
/* Raises the fatal error when `site` is armed, and disarms it. */
void async_test_fault_hit(async_test_fault_site_t site);
#define ASYNC_TEST_FAULT(site) async_test_fault_hit(site)
/* True when `site` is armed: disarms it (except ASYNC_TEST_IO_FAULT_WAIT_ALWAYS) and sets errno. */
bool async_test_io_fault_take(async_test_io_fault_t site);
#define ASYNC_TEST_IO_FAULT(site) async_test_io_fault_take(site)
#else
#define ASYNC_TEST_FAULT(site) ((void) 0)
#define ASYNC_TEST_IO_FAULT(site) false
#endif

#endif /* TRUE_ASYNC_TEST_HOOKS_H */
