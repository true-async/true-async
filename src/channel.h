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
#include "reactor.h"
#include "internal/zval_circular_buffer.h"

/* Async\Channel (dev/plans/S9-channel.md): values passed between the coroutines of one thread through
 * a bounded buffer or a rendezvous slot, TrueAsync's channel on this extension's wait records. */

/* __construct() ran: a second one throws. */
#define ASYNC_CHANNEL_F_CONSTRUCTED (1u << ASYNC_EVENT_F_TYPE_SHIFT)

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
	async_channel_close_reason_t timer_reason; /* the close the armed timer makes */
	/* The rendezvous value a close from a timer, the global deadlock or the owner scope rolled back, where no
	 * PHP code may run: released with the channel. */
	zval dropped_value;
	/* Parked receivers and senders in arrival order: the CHANNEL records of their coroutines and the queue
	 * entries of pending recvAsync() Futures. */
	async_wait_queue_t receivers;
	async_wait_queue_t senders;
	uint32_t reserved_receivers;    /* values promised to woken receivers that have not run yet */
	uint32_t reserved_senders;      /* free slots promised to woken senders */
	int32_t no_producer_timeout_ms; /* 0: none */
	int32_t no_consumer_timeout_ms; /* 0: none */
	/* While a waiter waits without a reservation and its side has a timeout: a Timer op on the reactor's
	 * waits with `hard_timeouts`, else one of its own ops; one reference. */
	async_io_event_t *timer;
	async_event_callback_t timer_callback;       /* in `timer`'s vector while armed */
	async_scope_t *owner_scope;                  /* NULL when bound to none, or once the scope is gone */
	async_event_callback_t owner_scope_callback; /* in the owner scope's vector while bound */
	zend_object std;
} async_channel_t;

extern zend_class_entry *async_ce_channel;
extern zend_class_entry *async_ce_channel_exception;
extern zend_class_entry *async_ce_channel_close_reason;

void async_register_channel_ce(void);

void async_channel_request_startup(void);

/* Withdraws the soft timers: called after the scheduler's shutdown and before the reactor's, which
 * asserts that its own ops are gone. A channel freed later finds no soft timer and no registry entry. */
void async_channel_request_shutdown(void);

/* The global deadlock (S9-channel.md 5): closes with DEADLOCK every channel that has a soft timer, which
 * wakes their waiters. False when there was none, and the deadlock is the scheduler's. Scheduler
 * context. */
bool async_channel_resolve_deadlocks(void);

/* The channel object whose queue holds the recvAsync() waiter `subscriber`, else NULL: for the collector
 * (collector.h), whoever holds the channel can complete the waiter's Future. */
zend_object *async_channel_of_future_waiter(const async_event_callback_t *subscriber);

/* For the collector: what closes `channel_object` without a holder, its armed timer and its owner
 * scope, makes it live, once per run. */
void async_channel_collector_sources(async_collector_t *collector, zend_object *channel_object);

/* For a scope's walks of its own vector (scope.c): whether `subscriber` binds a channel to the scope. */
bool async_channel_is_owner_scope_subscriber(const async_event_callback_t *subscriber);

/* Closes the channel `subscriber` binds with SCOPE_DISPOSED: the cancel or dispose of a scope that
 * completed or was cancelled before, which notifies nothing. */
void async_channel_close_for_owner_scope(const async_event_callback_t *subscriber);

#ifdef TRUE_ASYNC_TEST_HOOKS
/* The collector's oracle: the waiters of the channel `subscriber` binds that the collector found are
 * handed out, for a close by a route the collector leaves out (collector.h). */
void async_channel_hand_out_found(const async_event_callback_t *subscriber);
#endif

#endif /* TRUE_ASYNC_CHANNEL_H */
