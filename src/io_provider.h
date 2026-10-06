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
#ifndef TRUE_ASYNC_IO_PROVIDER_H
#define TRUE_ASYNC_IO_PROVIDER_H

/* The core's IO hooks provider (dev/plans/S6.md): the blocking wrappers hand their op to run(),
 * which parks the current coroutine on the reactor's queue until the op completes. */

#include "php.h"
#include "main/php_io_hooks.h"

/* The thread's provider, in ASYNC_G(io_provider). */
typedef struct
{
	php_io_hooks hooks;  /* the core hands back &hooks; io_provider_dtor() finds the rest */
	bool installed;      /* registered with the core in this request */
	bool installed_once; /* not installed again after its removal (RSHUTDOWN, or a replacement) */
} async_io_provider_t;

/* Resets the provider; nothing is registered until the first trigger. */
void async_io_provider_request_startup(void);

/* Unregisters the provider; called before async_reactor_request_shutdown(), so it is gone before
 * the queue is. */
void async_io_provider_request_shutdown(void);

/* Registers the provider with the core, with the current queue's flags, or none without a queue.
 * Does nothing once installed in this request; a refusal (another provider registered) is retried at
 * the next call. */
void async_io_provider_install(void);

/* The first install trigger: a coroutine other than main (S6.md section 2). One test per coroutine
 * once installed. */
#define ASYNC_IO_PROVIDER_INSTALL_ONCE() \
	do { \
		if (UNEXPECTED(!ASYNC_G(io_provider).installed_once)) { \
			async_io_provider_install(); \
		} \
	} while (0)

/* The second trigger: the reactor created `queue`. Gives an installed provider the queue's flags,
 * or installs it when no install happened yet in this request. */
void async_io_provider_queue_created(php_io_queue *queue);

/* A forked child's rebuild destroyed the queue: no registration capability until the next one. */
void async_io_provider_queue_destroyed(void);

#endif /* TRUE_ASYNC_IO_PROVIDER_H */
