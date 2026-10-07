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
 * blocks the number, and one SIGWAIT op on the reactor takes it. */

typedef struct _async_signal_watch_s async_signal_watch_t;

/* In ASYNC_G(signals) while a watch lives; NULL otherwise. */
typedef struct
{
	zend_object *context; /* never waited on: its add() blocks a number, its remove unblocks it */
	uint32_t count;       /* the watches */
#ifndef PHP_WIN32
	sigset_t watched;   /* their numbers */
	sigset_t reblocked; /* of them, blocked by async_signal_reblock() */
#endif
	async_signal_watch_t *watches[PHP_NSIG];
} async_signal_registry_t;

extern zend_class_entry *async_ce_signal;

#ifndef PHP_WIN32
/* Withdraws every watch: a Future still waiting stays pending. */
void async_signal_request_shutdown(void);

/* zend_sigaction() (Zend/zend_signal.c:258-263) and the script's pcntl_sigprocmask() unblock watched
 * numbers: blocks them again, before every poll of the reactor. */
void async_signal_reblock(void);

/* For the collector of coroutines that can never wake (collector.h): every Future a watch will
 * complete is live, since the watch's op, which the walk does not reach, delivers the signal. */
void async_signal_collector_seed(async_collector_t *collector);

/* In a forked child, at the end of the reactor's rebuild: the watches go to a new context and their
 * ops to the child's queue. A watch that cannot fails its Futures; no exception is left. */
void async_signal_rebuild(void);
#endif

#endif /* TRUE_ASYNC_OS_SIGNAL_H */
