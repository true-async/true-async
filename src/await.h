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
#ifndef TRUE_ASYNC_AWAIT_H
#define TRUE_ASYNC_AWAIT_H

#include "php.h"
#include "true_async_API.h"

/* Cancellation tokens of a wait and the await_* family (dev/plans/S5.md, sections 4 and 5). A token
 * is the awaitable of a Coroutine or a Future object; its completion ends the wait with
 * OperationCanceledException, whose previous is the token's exception. */

/* The awaitable of a Coroutine or a Future object; NULL with an AsyncException for a Future never
 * constructed (unserialize()). */
async_awaitable_t *async_await_awaitable_of(zend_object *object);

/* A reference for a wait to the coroutine's object or to the future event: the object a record's
 * target came from may let go of the event meanwhile (a second Future::__construct()). The release
 * may run PHP code. */
void async_awaitable_addref(async_awaitable_t *awaitable);
void async_awaitable_release(async_awaitable_t *awaitable);

/* Step 4 of S5.md section 4, before every link: a Future token is marked observed, and a token
 * that has completed throws OperationCanceledException. */
bool async_await_token_check(async_awaitable_t *token);

/* Links `record` of `waiter`'s wait on `token` (room reserved before): the token's completion
 * enqueues the waiter with OperationCanceledException. */
void async_await_token_link(async_coroutine_event_callback_t *record,
							async_coroutine_t *waiter,
							async_awaitable_t *token);

#endif /* TRUE_ASYNC_AWAIT_H */
