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
#ifndef TRUE_ASYNC_CHANNEL_H
#define TRUE_ASYNC_CHANNEL_H

#include "php.h"
#include "true_async_API.h"
#include "internal/zval_circular_buffer.h"

/* Async\Channel (dev/plans/S9-channel.md): values passed between the coroutines of one thread through
 * a bounded buffer or a rendezvous slot, TrueAsync's channel on this extension's wait records. */

/* Set on every channel event and on no other event: generic code tells a channel by it, as a Timeout by
 * ASYNC_TIMEOUT_F_TIMEOUT. */
#define ASYNC_CHANNEL_F_CHANNEL (1u << 29)
/* __construct() ran: a second one throws. */
#define ASYNC_CHANNEL_F_CONSTRUCTED (1u << ASYNC_EVENT_F_TYPE_SHIFT)

#define ASYNC_AWAITABLE_IS_CHANNEL(awaitable) \
	((((const async_awaitable_t *) (awaitable))->flags & (ASYNC_AWAITABLE_F_EVENT | ASYNC_CHANNEL_F_CHANNEL)) == \
	 (ASYNC_AWAITABLE_F_EVENT | ASYNC_CHANNEL_F_CHANNEL))

/* The cases of Async\ChannelCloseReason. */
typedef enum
{
	ASYNC_CHANNEL_CLOSE_EXPLICIT,
	ASYNC_CHANNEL_CLOSE_DISPOSED,
	ASYNC_CHANNEL_CLOSE_NO_PRODUCERS,
	ASYNC_CHANNEL_CLOSE_NO_CONSUMERS,
	ASYNC_CHANNEL_CLOSE_DEADLOCK,
	ASYNC_CHANNEL_CLOSE_SCOPE_DISPOSED,
} async_channel_close_reason_t;

/* Parked senders or receivers in arrival order: the CHANNEL records of their coroutines, borrowed, and
 * the queue entries of pending recvAsync() Futures. */
typedef struct
{
	async_coroutine_event_callback_t **records;
	uint32_t length;
	uint32_t capacity;
} async_channel_queue_t;

typedef struct
{
	async_event_t base;            /* ASYNC_EVENT_F_CLOSED once closed; ZEND_OBJ: inside `std`'s allocation */
	uint32_t capacity;             /* 0: a rendezvous */
	zval_circular_buffer_t buffer; /* capacity > 0: the values, oldest first */
	zval rendezvous_value;         /* capacity 0: the one value in the slot */
	bool rendezvous_has_value;
	bool rendezvous_committed; /* a receiver was woken for the slot's value: a close keeps it */
	bool hard_timeouts;
	async_channel_close_reason_t close_reason; /* valid once closed */
	zend_object *close_exception;              /* once closed: the outcome of the channel as an Awaitable */
	async_channel_queue_t receivers; /* coroutines and recvAsync() Futures */
	async_channel_queue_t senders;
	uint32_t reserved_receivers;    /* values promised to woken receivers that have not run yet */
	uint32_t reserved_senders;      /* free slots promised to woken senders */
	int32_t no_producer_timeout_ms; /* 0: none */
	int32_t no_consumer_timeout_ms; /* 0: none */
	zend_object std;
} async_channel_t;

extern zend_class_entry *async_ce_channel;
extern zend_class_entry *async_ce_channel_exception;
extern zend_class_entry *async_ce_channel_close_reason;

void async_register_channel_ce(void);
/* The channel object whose queue holds the recvAsync() waiter `subscriber`, else NULL: for the collector
 * (collector.h), whoever holds the channel can complete the waiter's Future. */
zend_object *async_channel_of_future_waiter(const async_event_callback_t *subscriber);

#endif /* TRUE_ASYNC_CHANNEL_H */
