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
#ifndef TRUE_ASYNC_OS_SIGNAL_H
#define TRUE_ASYNC_OS_SIGNAL_H

#include "php.h"
#include "main/php_poll.h"
#include "true_async_API.h"

/* Async\signal() (dev/plans/S6.md, section 8): one watch per signal number of the thread while a
 * Future waits for it. A watch's Io\Poll\SignalHandle sits in the thread's Io\Poll\Context, which
 * blocks the number, and one SIGWAIT op on the reactor takes it. On Windows the console's control
 * events take the place of signals (dev/plans/S10.md section 8, W1): Ctrl+C, Ctrl+Break and the
 * console's close arrive as SIGINT, SIGBREAK and SIGHUP through a trigger of the main thread. */

#ifdef PHP_WIN32
/* Windows has no number of its own for most cases: a watch is kept under the enum's number, which
 * goes up to SIGWINCH's 28. */
#define ASYNC_SIGNAL_SLOTS 29
#else
#define ASYNC_SIGNAL_SLOTS PHP_NSIG
#endif

typedef struct _async_signal_watch_s async_signal_watch_t;

/* In ASYNC_G(signals) while a watch lives; NULL otherwise. */
typedef struct
{
#ifndef PHP_WIN32
	zend_object *context; /* never waited on: its add() blocks a number, its remove unblocks it */
	sigset_t watched;     /* the watches' numbers */
#else
	async_event_callback_t console_callback; /* in the vector of the trigger the console handler fires */
#endif
	uint32_t count; /* the watches */
	async_signal_watch_t *watches[ASYNC_SIGNAL_SLOTS];
} async_signal_registry_t;

extern zend_class_entry *async_ce_signal;

/* Withdraws every watch: a Future still waiting stays pending. */
void async_signal_request_shutdown(void);

/* For the collector of coroutines that can never wake (collector.h): every Future a watch will
 * complete is live, since the watch, which the walk does not reach, delivers the signal. */
void async_signal_collector_seed(async_collector_t *collector);

#ifdef PHP_WIN32
/* Installs the console handler in the CLI, where the core's sapi_windows_set_ctrl_handler() works
 * (win32/signal.c); elsewhere Async\signal() throws. False when Windows refuses the handler. */
bool async_signal_module_startup(void);

void async_signal_module_shutdown(void);
#else
/* zend_sigaction() outside pcntl (Zend/zend_signal.c:260-263) unblocks watched numbers. Blocks them
 * again before every poll of the reactor and records them with the core, so the last removal of their
 * handles unblocks them. */
void async_signal_reblock(void);

/* In a forked child, at the end of the reactor's rebuild: the watches go to a new context and their
 * ops to the child's queue. A watch that cannot fails its Futures; no exception is left. */
void async_signal_rebuild(void);
#endif

#endif /* TRUE_ASYNC_OS_SIGNAL_H */
