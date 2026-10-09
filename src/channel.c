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
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "php.h"
#include "zend_enum.h"
#include "zend_exceptions.h"
#include "zend_interfaces.h"
#include "php_true_async.h"
#include "await.h"
#include "channel.h"
#include "collector.h"
#include "coroutine.h"
#include "exceptions.h"
#include "future.h"
#include "scheduler.h"
#include "scope.h"
#include "channel_arginfo.h"

/* Ports TrueAsync's channel (channel.c of ext/async, cited below by line) onto this extension's wait records.
 * A parked send() or recv() links its coroutine's first waker record with the CHANNEL kind and puts a
 * pointer to it in its side's queue: the record is the queue entry, so parking allocates nothing (D29).
 * The channel wakes a waiter itself, not through a notify, and the record stays linked until the frame
 * that parked it removes it, which tells that frame what happened while it was away (section 3 of
 * dev/plans/S9-channel.md).
 *
 * A wake only queues a coroutine, so the value or slot it was woken for is reserved for it until it
 * runs: TrueAsync's reservations (channel.c:182-193, 431-519), which no other sender or receiver may
 * take. */

zend_class_entry *async_ce_channel = NULL;
zend_class_entry *async_ce_channel_exception = NULL;
zend_class_entry *async_ce_channel_close_reason = NULL;

static zend_object_handlers channel_handlers;

/* Bits of a CHANNEL record's flags. */
#define CHANNEL_RECORD_F_SENDER (1u << ASYNC_CALLBACK_F_KIND_SHIFT)
/* A value (a receiver) or a free slot (a sender) is held for the record's coroutine. */
#define CHANNEL_RECORD_F_RESERVED (1u << (ASYNC_CALLBACK_F_KIND_SHIFT + 1))
/* A rendezvous sender parked on its own value in the slot: it reserves nothing. */
#define CHANNEL_RECORD_F_DELIVERING (1u << (ASYNC_CALLBACK_F_KIND_SHIFT + 2))
/* The iterator lives in a C local the collector's walk does not read (channel_iterator_is_c_local()):
 * the wait owns its reference to the channel. */
#define CHANNEL_RECORD_F_HOLDS_CHANNEL (1u << (ASYNC_CALLBACK_F_KIND_SHIFT + 3))

#define CHANNEL_QUEUE_FIRST_CAPACITY 4

/* A pending recvAsync() Future's place in the receivers' queue (channel.c:110-139). The Future's event
 * disposes `on_future` when it completes or is freed, which frees the waiter, so a dropped Future leaves
 * the queue. */
typedef struct
{
	async_coroutine_event_callback_t queue_record; /* `event`: the channel while queued, NULL once out */
	async_event_callback_t on_future;
	async_future_event_t *future; /* borrowed: the event disposes `on_future` before it goes */
} channel_future_waiter_t;

#define CHANNEL_FUTURE_WAITER_OF(member_pointer, member) \
	((channel_future_waiter_t *) ((char *) (member_pointer) - offsetof(channel_future_waiter_t, member)))

/* A recvAsync() Future's queue entry: no coroutine, never reserved. */
static zend_always_inline bool channel_record_is_future(const async_coroutine_event_callback_t *record)
{
	return record->coroutine == NULL;
}

static zend_always_inline async_channel_t *channel_from_object(zend_object *object)
{
	return (async_channel_t *) ((char *) object - offsetof(async_channel_t, std));
}

#define THIS_CHANNEL channel_from_object(Z_OBJ_P(ZEND_THIS))

static zend_always_inline bool channel_is_closed(const async_channel_t *channel)
{
	return (channel->base.flags & ASYNC_EVENT_F_CLOSED) != 0;
}

///////////////////////////////////////////////////////////////////
/// Close reasons
///////////////////////////////////////////////////////////////////

static const char *const channel_close_case_names[] = {
	[ASYNC_CHANNEL_CLOSE_EXPLICIT] = "EXPLICIT",         [ASYNC_CHANNEL_CLOSE_DISPOSED] = "DISPOSED",
	[ASYNC_CHANNEL_CLOSE_NO_PRODUCERS] = "NO_PRODUCERS", [ASYNC_CHANNEL_CLOSE_NO_CONSUMERS] = "NO_CONSUMERS",
	[ASYNC_CHANNEL_CLOSE_DEADLOCK] = "DEADLOCK",         [ASYNC_CHANNEL_CLOSE_SCOPE_DISPOSED] = "SCOPE_DISPOSED",
};

static const char *const channel_close_messages[] = {
	[ASYNC_CHANNEL_CLOSE_EXPLICIT] = "Channel is closed",
	[ASYNC_CHANNEL_CLOSE_DISPOSED] = "Channel disposed",
	[ASYNC_CHANNEL_CLOSE_NO_PRODUCERS] = "Channel deadlock: no producers",
	[ASYNC_CHANNEL_CLOSE_NO_CONSUMERS] = "Channel deadlock: no consumers",
	[ASYNC_CHANNEL_CLOSE_DEADLOCK] = "Channel deadlock",
	[ASYNC_CHANNEL_CLOSE_SCOPE_DISPOSED] = "Channel closed: owner scope disposed",
};

/* A ChannelException whose message and `reason` name `reason` (channel.c:83-104). */
static zend_object *channel_exception_new(const async_channel_close_reason_t reason)
{
	zend_object *const exception =
			async_new_exception(async_ce_channel_exception, "%s", channel_close_messages[reason]);
	zval reason_case;

	ZVAL_OBJ_COPY(&reason_case,
				  zend_enum_get_case_cstr(async_ce_channel_close_reason, channel_close_case_names[reason]));
	zend_update_property(async_ce_channel_exception, exception, ZEND_STRL("reason"), &reason_case);
	zval_ptr_dtor(&reason_case);

	return exception;
}

static void channel_throw_closed(const async_channel_t *channel)
{
	zend_throw_exception_internal(channel_exception_new(channel->close_reason));
}

///////////////////////////////////////////////////////////////////
/// Values and slots
///////////////////////////////////////////////////////////////////

static zend_always_inline bool channel_is_buffered(const async_channel_t *channel)
{
	return channel->capacity > 0;
}

/* Values held, the promised ones included. */
static zend_always_inline uint32_t channel_count(const async_channel_t *channel)
{
	if (channel_is_buffered(channel)) {
		return channel->buffer.count;
	}

	return channel->rendezvous_has_value ? 1 : 0;
}

static zend_always_inline uint32_t channel_free_space(const async_channel_t *channel)
{
	if (channel_is_buffered(channel)) {
		return channel->capacity - channel->buffer.count;
	}

	return channel->rendezvous_has_value ? 0 : 1;
}

/* A value no woken receiver was promised: anyone may take it. */
static zend_always_inline bool channel_has_free_value(const async_channel_t *channel)
{
	return channel_count(channel) > channel->reserved_receivers;
}

/* A slot no woken sender was promised. */
static zend_always_inline bool channel_has_free_slot(const async_channel_t *channel)
{
	return channel_free_space(channel) > channel->reserved_senders;
}

/* Moves the slot's value into `result` and empties the slot. */
static void channel_rendezvous_move(async_channel_t *channel, zval *result)
{
	ZVAL_COPY_VALUE(result, &channel->rendezvous_value);
	ZVAL_UNDEF(&channel->rendezvous_value);
	channel->rendezvous_has_value = false;
	channel->rendezvous_committed = false;
}

/* Moves the oldest value into `result`; the caller has a free value or a reservation of its own. */
static void channel_take_value(async_channel_t *channel, zval *result)
{
	if (channel_is_buffered(channel)) {
		zval_circular_buffer_pop(&channel->buffer, result);
		return;
	}

	channel_rendezvous_move(channel, result);
}

/* Moves an uncommitted rendezvous value into `dropped`: nobody was told it was delivered, and a later
 * recv() must not receive it (channel.c:577-585, 733-743). */
static void channel_withdraw_rendezvous_value(async_channel_t *channel, zval *dropped)
{
	if (!channel->rendezvous_has_value || channel->rendezvous_committed) {
		return;
	}

	channel_rendezvous_move(channel, dropped);
}

///////////////////////////////////////////////////////////////////
/// Queues
///////////////////////////////////////////////////////////////////

/* Makes room for one more record; may allocate, so a wait calls it before its first link. */
static void channel_queue_make_room(async_channel_queue_t *queue)
{
	if (EXPECTED(queue->length < queue->capacity)) {
		return;
	}

	queue->capacity = queue->capacity == 0 ? CHANNEL_QUEUE_FIRST_CAPACITY : queue->capacity * 2;
	queue->records = safe_erealloc(queue->records, queue->capacity, sizeof(*queue->records), 0);
}

static zend_always_inline void channel_queue_push(async_channel_queue_t *queue,
												  async_coroutine_event_callback_t *record)
{
	ZEND_ASSERT(queue->length < queue->capacity);

	queue->records[queue->length++] = record;
}

/* Keeps the arrival order, which decides who a handed-on reservation reaches (channel.c:248-257). */
static void channel_queue_remove_at(async_channel_queue_t *queue, const uint32_t index)
{
	queue->length--;

	if (index < queue->length) {
		memmove(&queue->records[index], &queue->records[index + 1], (queue->length - index) * sizeof(*queue->records));
	}
}

/* False when the record is not in the queue. */
static bool channel_queue_remove(async_channel_queue_t *queue, const async_coroutine_event_callback_t *record)
{
	for (uint32_t i = 0; i < queue->length; i++) {
		if (queue->records[i] == record) {
			channel_queue_remove_at(queue, i);
			return true;
		}
	}

	return false;
}

/* The oldest record with nothing promised to it: a reserved one keeps its place until it runs, and a
 * delivering sender waits for an answer, not a slot (channel.c:259-271). */
static async_coroutine_event_callback_t *channel_queue_first_unreserved(const async_channel_queue_t *queue)
{
	for (uint32_t i = 0; i < queue->length; i++) {
		if (!(queue->records[i]->event_callback.flags & (CHANNEL_RECORD_F_RESERVED | CHANNEL_RECORD_F_DELIVERING))) {
			return queue->records[i];
		}
	}

	return NULL;
}

/* The rendezvous sender parked on the slot's value; at most one (channel.c:273-284). */
static async_coroutine_event_callback_t *channel_queue_delivering(const async_channel_queue_t *queue)
{
	for (uint32_t i = 0; i < queue->length; i++) {
		if (queue->records[i]->event_callback.flags & CHANNEL_RECORD_F_DELIVERING) {
			return queue->records[i];
		}
	}

	return NULL;
}

static void channel_queue_free(async_channel_queue_t *queue)
{
	ZEND_ASSERT(queue->length == 0 && "a parked waiter holds its channel through its frame");

	if (queue->records != NULL) {
		efree(queue->records);
	}
}

///////////////////////////////////////////////////////////////////
/// The deadlock timer
///////////////////////////////////////////////////////////////////

/* TrueAsync's per-channel timer (channel.c:323-422): while a side waits with no reservation, its timeout
 * closes the channel with NO_PRODUCERS or NO_CONSUMERS. A hard timer is a Timer op on the reactor's
 * waits, so a script that ends by itself waits for it, as Scope::disposeAfterTimeout()'s; a soft one is
 * one of the reactor's own ops, which fires while anything else keeps the loop running and keeps nothing
 * from the global deadlock, where async_channel_resolve_deadlocks() closes its channel (S9-channel.md 5). */

static void channel_close(async_channel_t *channel, async_channel_close_reason_t reason, zval *dropped);

/* A forked child's rebuild drops a hard timer unrun (scope.c's check). */
static zend_always_inline bool channel_timer_is_armed(const async_channel_t *channel)
{
	return channel->timer != NULL && channel->timer->reactor_link.prev != NULL;
}

/* A completed op withdraws as nothing. */
static void channel_timer_withdraw(async_channel_t *channel)
{
	async_io_event_t *const timer = channel->timer;

	channel->timer = NULL;
	async_io_event_orphan(timer);
	async_callbacks_remove(&timer->base.callbacks, &channel->timer_callback);
	async_io_event_release(timer);
}

static void channel_deadlock_registry_remove(const async_channel_t *channel)
{
	if (EXPECTED(!channel->hard_timeouts)) {
		zend_hash_index_del(&ASYNC_G(deadlock_channels), channel->std.handle);
	}
}

static void channel_timer_disarm(async_channel_t *channel)
{
	if (EXPECTED(channel->timer == NULL)) {
		return;
	}

	channel_deadlock_registry_remove(channel);
	channel_timer_withdraw(channel);
}

/* In scheduler context: the rolled-back value waits for the channel's free. */
static void
channel_timer_fire(async_awaitable_t *target, async_event_callback_t *callback, void *result, zend_object *exception)
{
	(void) target;
	(void) result;
	(void) exception;

	async_channel_t *const channel =
			(async_channel_t *) ((char *) callback - offsetof(async_channel_t, timer_callback));

	channel_close(channel, channel->timer_reason, &channel->dropped_value);
}

/* Arms nothing once async is off: a destructor run from `released_values` may still send or receive.
 * `may_throw`: a failed submit leaves its Error pending; otherwise it leaves only no timer. */
static void channel_timer_arm(async_channel_t *channel, const async_channel_close_reason_t reason, const bool may_throw)
{
	const int32_t timeout_ms = reason == ASYNC_CHANNEL_CLOSE_NO_PRODUCERS ? channel->no_producer_timeout_ms
																		  : channel->no_consumer_timeout_ms;

	if (timeout_ms == 0 || UNEXPECTED(channel_is_closed(channel) || !ZEND_ASYNC_IS_ACTIVE)) {
		return;
	}

	/* A Timer op never completes in its submit (reactor.c, queue_push). */
	async_io_event_t *const timer = async_io_event_new();

	php_io_op_timer(&timer->op, async_reactor_deadline_from_ms(timeout_ms));
	async_callbacks_reserve(&timer->base.callbacks, 1);
	async_callbacks_push_reserved(&timer->base.callbacks, &channel->timer_callback);

	/* The entry first: after a bailout in its insert there is no timer, and after one in the submit
	 * RSHUTDOWN's walk withdraws it, as an op not submitted withdraws as nothing. */
	if (EXPECTED(!channel->hard_timeouts)) {
		zend_hash_index_add_new_ptr(&ASYNC_G(deadlock_channels), channel->std.handle, channel);
	}

	channel->timer = timer;
	channel->timer_reason = reason;

	bool is_submitted;

	if (may_throw) {
		is_submitted =
				(channel->hard_timeouts ? async_io_event_submit(timer) : async_reactor_submit_own(timer)) == SUCCESS;
	} else {
		is_submitted =
				(channel->hard_timeouts ? async_io_event_try_submit(timer) : async_reactor_try_submit_own(timer)) == 0;
	}

	if (UNEXPECTED(!is_submitted)) {
		channel->timer = NULL;
		channel_deadlock_registry_remove(channel);
		async_callbacks_remove(&timer->base.callbacks, &channel->timer_callback);
		async_io_event_release(timer);
	}
}

/* Whether a side waits with no reservation, and the close its timer makes: a reserved waiter is not
 * starving. Queued recvAsync() Futures count as receivers. */
static bool channel_has_starving_side(const async_channel_t *channel, async_channel_close_reason_t *reason)
{
	if (channel->receivers.length > channel->reserved_receivers) {
		*reason = ASYNC_CHANNEL_CLOSE_NO_PRODUCERS;
		return true;
	}

	if (channel->senders.length > channel->reserved_senders) {
		*reason = ASYNC_CHANNEL_CLOSE_NO_CONSUMERS;
		return true;
	}

	return false;
}

/* After a park, a wake or a wait's end, not when a Future is queued (channel.c:401-422), so pending
 * Futures alone arm a timer only once a wake or a wait's end finds them starving; a waiter that leaves
 * unwoken only disarms (channel_timer_disarm_if_idle()). A timer runs from its first arming while its side
 * keeps starving. Only a park's arm may throw, as its wait then ends before it parks: elsewhere a value may have
 * moved, and a failed submit leaves the channel without a timer until the next refresh. */
static void channel_timer_refresh(async_channel_t *channel, const bool may_throw)
{
	async_channel_close_reason_t reason;

	if (EXPECTED(channel->no_producer_timeout_ms == 0 && channel->no_consumer_timeout_ms == 0)) {
		return;
	}

	if (!channel_has_starving_side(channel, &reason)) {
		channel_timer_disarm(channel);
		return;
	}

	if (channel->timer != NULL) {
		/* The rebuild runs lazily, so it goes before the check; it resubmits a soft timer itself. */
		if (UNEXPECTED(channel->hard_timeouts)) {
			async_reactor_check_fork();
		}

		if (EXPECTED(channel->timer_reason == reason && channel_timer_is_armed(channel))) {
			return;
		}

		channel_timer_disarm(channel);
	}

	channel_timer_arm(channel, reason, may_throw);
}

/* After a waiter leaves without a wake: disarms a timer whose side starves no more, and arms nothing, so it
 * runs anywhere. */
static void channel_timer_disarm_if_idle(async_channel_t *channel)
{
	async_channel_close_reason_t reason;

	if (!channel_has_starving_side(channel, &reason) || reason != channel->timer_reason) {
		channel_timer_disarm(channel);
	}
}

void async_channel_request_startup(void)
{
	zend_hash_init(&ASYNC_G(deadlock_channels), 8, NULL, NULL, false);
}

/* After a fatal error no destructor closed the channels, and free_obj comes after RSHUTDOWN. */
void async_channel_request_shutdown(void)
{
	async_channel_t *channel;

	ZEND_HASH_FOREACH_PTR(&ASYNC_G(deadlock_channels), channel)
	{
		channel_timer_withdraw(channel);
	}
	ZEND_HASH_FOREACH_END();

	zend_hash_destroy(&ASYNC_G(deadlock_channels));
}

///////////////////////////////////////////////////////////////////
/// Wakes
///////////////////////////////////////////////////////////////////

/* The record stays linked for its frame. Takes `exception`: the waker chains an error it finds pending
 * under it, so no two waiters may share one. */
static void channel_record_wake(const async_coroutine_event_callback_t *record, zend_object *exception)
{
#ifdef TRUE_ASYNC_TEST_HOOKS
	async_collector_check_event_wake(record->coroutine);
#endif

	async_scheduler_enqueue(&record->coroutine->coroutine, exception, true);
}

/* Promises what was freed to `record` and wakes it. The record stays queued, and its coroutine is woken
 * even when a cancel queued it already: its frame then hands the reservation on. */
static void channel_promise(async_coroutine_event_callback_t *record, uint32_t *reserved_count)
{
	record->event_callback.flags |= CHANNEL_RECORD_F_RESERVED;
	(*reserved_count)++;
	channel_record_wake(record, NULL);
}

static void channel_wake_sender(async_channel_t *channel);

/* The caller owes the freed slot to a sender: channel_wake_sender(). */
static void channel_future_give_value(async_channel_t *channel, async_future_event_t *future)
{
	zval value;

	channel_take_value(channel, &value);
	async_future_event_resolve(future, &value, NULL);
	zval_ptr_dtor(&value);
}

static void channel_future_reject(async_future_event_t *future, const async_channel_close_reason_t reason)
{
	zend_object *const exception = channel_exception_new(reason);

	async_future_event_resolve(future, NULL, exception);
	OBJ_RELEASE(exception);
}

/* A Future has no later run, so it takes the value at once and reserves nothing (channel.c:448-460). */
static void channel_future_serve(async_channel_t *channel, async_coroutine_event_callback_t *record)
{
	channel_queue_remove(&channel->receivers, record);
	record->event = NULL;
	/* Disposes the waiter. */
	channel_future_give_value(channel, CHANNEL_FUTURE_WAITER_OF(record, queue_record)->future);
	channel_wake_sender(channel);
}

/* Gives a free value to the oldest receiver without a reservation: a Future takes it, a coroutine is
 * promised it. False when there is no such value or receiver (channel.c:431-470). */
static bool channel_wake_receiver(async_channel_t *channel)
{
	if (!channel_has_free_value(channel)) {
		return false;
	}

	async_coroutine_event_callback_t *const record = channel_queue_first_unreserved(&channel->receivers);

	if (record == NULL) {
		return false;
	}

	if (UNEXPECTED(channel_record_is_future(record))) {
		channel_future_serve(channel, record);
	} else {
		channel_promise(record, &channel->reserved_receivers);
	}

	channel_timer_refresh(channel, false);

	return true;
}

/* The rendezvous sender whose value was just taken leaves its queue and is woken: a close before it
 * runs must not fail a message the receiver holds (channel.c:472-492). */
static void channel_wake_delivered_sender(async_channel_t *channel)
{
	if (channel_is_buffered(channel) || channel->rendezvous_has_value) {
		return;
	}

	async_coroutine_event_callback_t *const record = channel_queue_delivering(&channel->senders);

	if (record == NULL) {
		return;
	}

	channel_queue_remove(&channel->senders, record);
	channel_record_wake(record, NULL);
}

/* Pays both debts a freed slot owes, in one call so a sender arriving in between cannot take the slot
 * (channel.c:494-519): the sender whose value was taken hears so, and the oldest sender waiting for a
 * slot is promised this one. */
static void channel_wake_sender(async_channel_t *channel)
{
	channel_wake_delivered_sender(channel);

	if (channel_has_free_slot(channel)) {
		async_coroutine_event_callback_t *const record = channel_queue_first_unreserved(&channel->senders);

		if (record != NULL) {
			channel_promise(record, &channel->reserved_senders);
		}
	}

	channel_timer_refresh(channel, false);
}

/* Closes the channel once; the first reason stays (channel.c:521-586). A reserved receiver stays queued:
 * its value is still here and its recv() takes it. Each other waiter gets an exception of its own, since
 * the waker chains a cancellation it finds queued under the one it is given (S9-channel.md 8, item 15).
 * The walks go from the tail and reread the queue's end at each step: a Future freed while another is
 * rejected (PHP's collector, a destructor) leaves the receivers' queue from under the walk, and a removal
 * only moves records down, so none the walk has yet to reach is skipped. An uncommitted
 * rendezvous value moves to `dropped`, which the caller releases once the channel is consistent: its
 * destructor may use the channel. */
static void channel_close(async_channel_t *channel, const async_channel_close_reason_t reason, zval *dropped)
{
	if (channel_is_closed(channel)) {
		return;
	}

	channel->close_reason = reason;
	channel->base.flags |= ASYNC_EVENT_F_CLOSED;
	channel_timer_disarm(channel);

	uint32_t index = channel->receivers.length;

	while (index > 0) {
		index--;

		if (UNEXPECTED(index >= channel->receivers.length)) {
			index = channel->receivers.length;
			continue;
		}

		async_coroutine_event_callback_t *const record = channel->receivers.records[index];

		if (record->event_callback.flags & CHANNEL_RECORD_F_RESERVED) {
			continue;
		}

		channel_queue_remove_at(&channel->receivers, index);

		if (UNEXPECTED(channel_record_is_future(record))) {
			record->event = NULL;
			/* Disposes the waiter. */
			channel_future_reject(CHANNEL_FUTURE_WAITER_OF(record, queue_record)->future, reason);
		} else {
			channel_record_wake(record, channel_exception_new(reason));
		}
	}

	while (channel->senders.length > 0) {
		const uint32_t last = channel->senders.length - 1;
		async_coroutine_event_callback_t *const record = channel->senders.records[last];

		channel_queue_remove_at(&channel->senders, last);
		channel_record_wake(record, channel_exception_new(reason));
	}

	channel_withdraw_rendezvous_value(channel, dropped);
}

static void channel_close_and_release(async_channel_t *channel, const async_channel_close_reason_t reason)
{
	zval dropped;

	ZVAL_UNDEF(&dropped);
	channel_close(channel, reason, &dropped);
	zval_ptr_dtor(&dropped);
}

///////////////////////////////////////////////////////////////////
/// The owner scope
///////////////////////////////////////////////////////////////////

/* TrueAsync's binding (channel.c:592-638): the channel closes with SCOPE_DISPOSED when its scope is
 * cancelled or freed, not when it completes (S9-channel.md 5). The closes run inside the scope's walks,
 * where no PHP code may run. */

static zend_always_inline async_channel_t *channel_of_owner_scope_callback(const async_event_callback_t *callback)
{
	return (async_channel_t *) ((char *) callback - offsetof(async_channel_t, owner_scope_callback));
}

void async_channel_close_for_owner_scope(const async_event_callback_t *subscriber)
{
	async_channel_t *const channel = channel_of_owner_scope_callback(subscriber);

	channel_close(channel, ASYNC_CHANNEL_CLOSE_SCOPE_DISPOSED, &channel->dropped_value);
}

/* The scope's cancel and its error route notify with an error; its completion notifies without one. */
static void channel_owner_scope_notified(async_awaitable_t *target,
										 async_event_callback_t *callback,
										 void *result,
										 zend_object *exception)
{
	(void) target;
	(void) result;

	if (exception != NULL) {
		async_channel_close_for_owner_scope(callback);
	}
}

/* The scope is freed before the channel. Once async is off, RSHUTDOWN's free of the request's scopes and a scope
 * object's free in zend_deactivate() leave the channel to its own free. */
static void channel_owner_scope_dispose(async_event_callback_t *callback, async_awaitable_t *target)
{
	(void) target;

	async_channel_t *const channel = channel_of_owner_scope_callback(callback);

	channel->owner_scope = NULL;

	if (EXPECTED(ZEND_ASYNC_IS_ACTIVE)) {
		async_channel_close_for_owner_scope(callback);
	}
}

/* To the current scope, the global one at the top level; none once the global scope is gone (a
 * destructor run from `released_values`) or when the scope is closed (channel.c:611-627). */
static void channel_bind_to_owner_scope(async_channel_t *channel)
{
	async_scope_t *const scope = async_scope_current();

	if (UNEXPECTED(scope == NULL || (scope->event.flags & ASYNC_SCOPE_F_CLOSED))) {
		return;
	}

	async_callbacks_reserve(&scope->event.callbacks, 1);
	async_callbacks_push_reserved(&scope->event.callbacks, &channel->owner_scope_callback);
	channel->owner_scope = scope;
}

/* At the channel's free only: a close leaves the subscriber in place, since the scope's teardown walks
 * its vector with swap removals and would skip a subscriber that another removed. */
static void channel_unbind_from_owner_scope(async_channel_t *channel)
{
	async_scope_t *const scope = channel->owner_scope;

	if (scope != NULL) {
		channel->owner_scope = NULL;
		async_callbacks_remove(&scope->event.callbacks, &channel->owner_scope_callback);
	}
}

bool async_channel_is_owner_scope_subscriber(const async_event_callback_t *subscriber)
{
	return !(subscriber->flags & ASYNC_CALLBACK_F_RECORD) && subscriber->callback == channel_owner_scope_notified;
}

///////////////////////////////////////////////////////////////////
/// The global deadlock
///////////////////////////////////////////////////////////////////

#ifdef TRUE_ASYNC_TEST_HOOKS
static void channel_hand_out_found_record(const async_coroutine_event_callback_t *record)
{
	zend_coroutine_t *const waiter = &record->coroutine->coroutine;

	if (UNEXPECTED(waiter->flags & ASYNC_COROUTINE_F_DEADLOCK_FOUND)) {
		waiter->flags |= ASYNC_COROUTINE_F_HANDED_OUT;
	}
}

static void channel_hand_out_found_records(async_callbacks_vector_t *callbacks)
{
	async_event_callback_t **const callback_slots = async_callbacks_slots(callbacks);

	for (uint32_t i = 0; i < callbacks->length; i++) {
		if (EXPECTED(callback_slots[i]->flags & ASYNC_CALLBACK_F_RECORD)) {
			channel_hand_out_found_record((const async_coroutine_event_callback_t *) callback_slots[i]);
		}
	}
}

/* Every waiter a close would wake: the queued coroutines and the awaiters of the queued Futures. */
static void channel_hand_out_found_waiters(async_channel_t *channel)
{
	const async_channel_queue_t *const queues[] = { &channel->receivers, &channel->senders };

	for (uint32_t queue_index = 0; queue_index < sizeof(queues) / sizeof(queues[0]); queue_index++) {
		const async_channel_queue_t *const queue = queues[queue_index];

		for (uint32_t i = 0; i < queue->length; i++) {
			const async_coroutine_event_callback_t *const record = queue->records[i];

			if (UNEXPECTED(channel_record_is_future(record))) {
				channel_hand_out_found_records(&CHANNEL_FUTURE_WAITER_OF(record, queue_record)->future->base.callbacks);
			} else {
				channel_hand_out_found_record(record);
			}
		}
	}
}

void async_channel_hand_out_found(const async_event_callback_t *subscriber)
{
	channel_hand_out_found_waiters(channel_of_owner_scope_callback(subscriber));
}
#endif

/* TrueAsync's async_channel_resolve_deadlocks() (channel.c:640-674). A close leaves the registry, so the
 * walk takes the first entry until none is left. The global deadlock is a route the collector leaves out
 * (S7.md 2): what it found there is handed out first. */
bool async_channel_resolve_deadlocks(void)
{
	HashTable *const registry = &ASYNC_G(deadlock_channels);

	if (EXPECTED(zend_hash_num_elements(registry) == 0)) {
		return false;
	}

	while (zend_hash_num_elements(registry) != 0) {
		zend_hash_internal_pointer_reset(registry);

		async_channel_t *const channel = zend_hash_get_current_data_ptr(registry);

#ifdef TRUE_ASYNC_TEST_HOOKS
		channel_hand_out_found_waiters(channel);
#endif
		channel_close(channel, ASYNC_CHANNEL_CLOSE_DEADLOCK, &channel->dropped_value);
	}

	return true;
}

///////////////////////////////////////////////////////////////////
/// The CHANNEL wait kind
///////////////////////////////////////////////////////////////////

static zend_always_inline async_channel_t *channel_of_record(const async_coroutine_event_callback_t *record)
{
	return (async_channel_t *) record->event;
}

/* `role`: a record's SENDER bit, alone or with the others. */
static zend_always_inline async_channel_queue_t *channel_queue_of_role(async_channel_t *channel, const uint32_t role)
{
	return (role & CHANNEL_RECORD_F_SENDER) ? &channel->senders : &channel->receivers;
}

/* Takes the record out of its queue, gives back a reservation it holds and clears its `event`; true
 * when it was still queued. Whether someone else gets the reservation is the caller's decision. */
static bool channel_record_leave(async_coroutine_event_callback_t *record, bool *had_reservation)
{
	async_channel_t *const channel = channel_of_record(record);
	const uint32_t flags = record->event_callback.flags;
	const bool was_queued = channel_queue_remove(channel_queue_of_role(channel, flags), record);

	*had_reservation = (flags & CHANNEL_RECORD_F_RESERVED) != 0;

	if (*had_reservation) {
		if (flags & CHANNEL_RECORD_F_SENDER) {
			ZEND_ASSERT(channel->reserved_senders > 0 && "a sender reservation is released once");
			channel->reserved_senders--;
		} else {
			ZEND_ASSERT(channel->reserved_receivers > 0 && "a receiver reservation is released once");
			channel->reserved_receivers--;
		}
	}

	record->event = NULL;

	return was_queued;
}

static zend_string *channel_record_info(const async_coroutine_event_callback_t *record)
{
	const async_channel_t *const channel = channel_of_record(record);

	return zend_strpprintf(0,
						   "Channel(capacity=%u, receivers=%u, senders=%u, reserved receivers=%u, reserved senders=%u)",
						   channel->capacity,
						   channel->receivers.length,
						   channel->senders.length,
						   channel->reserved_receivers,
						   channel->reserved_senders);
}

/* For a frame that never runs again: a bailout's unwinding or the request's end, where nothing may be
 * queued and no PHP code may run; and for a park that failed before it suspended (channel_wait_link()). The record
 * leaves its queue and gives its reservation back without waking anyone; a delivering sender's value stays in the slot
 * for the next receiver. The timer goes with the last starving waiter, and none is armed here. */
static void channel_record_abort(async_coroutine_event_callback_t *record)
{
	async_channel_t *const channel = channel_of_record(record);
	bool had_reservation;

	channel_record_leave(record, &had_reservation);
	channel_timer_disarm_if_idle(channel);
}

/* What closes the channel without a holder (S9-channel.md 6): an armed timer fires by itself, and the
 * owner scope closes it once something can cancel the scope or, for a scope that its last member's end
 * frees, once a coroutine of its subtree can run. Either makes the channel live, and with it the
 * coroutines parked on it and the awaiters of its queued Futures. The request's two scopes are left
 * out: only exit() and an unhandled error cancel them, routes the collector leaves out. */
void async_channel_collector_sources(async_collector_t *collector, zend_object *channel_object)
{
	async_channel_t *const channel = channel_from_object(channel_object);
	bool is_new_node;
	const uint32_t node = async_collector_reach_node(collector, &channel->owner_scope_callback, &is_new_node);

	/* Once per run: every pass meets the channel again. */
	if (EXPECTED(!is_new_node)) {
		return;
	}

	async_collector_report_reach_to_object(collector, node, channel_object);

	if (channel_timer_is_armed(channel)) {
		async_collector_report_live_reach(collector, node);
	}

	async_scope_t *const scope = channel->owner_scope;

	if (scope != NULL && !(scope->event.flags & ASYNC_SCOPE_F_REQUEST_LIFETIME)) {
		async_scope_collector_bound_channel_reach(collector, scope, node);
	}
}

/* Whoever holds the channel can send, receive or close. */
static void channel_record_collector_target(const async_coroutine_event_callback_t *record,
											async_collector_t *collector)
{
	async_channel_t *const channel = channel_of_record(record);

	async_collector_report_target(
			collector, &channel->std, (record->event_callback.flags & CHANNEL_RECORD_F_HOLDS_CHANNEL) != 0);
	async_channel_collector_sources(collector, &channel->std);
}

static const async_wait_kind_t channel_wait_kind = {
	.info = channel_record_info,
	.abort = channel_record_abort,
	.collector_target = channel_record_collector_target,
};

/* Links the current coroutine's record into its role's queue and its token record and returns the
 * queue's record; NULL with an exception, nothing linked, when there is no coroutine to park, the
 * token refuses or the timer's submit fails. */
static async_coroutine_event_callback_t *
channel_wait_link(async_channel_t *channel, async_awaitable_t *const token, const uint32_t role)
{
	async_channel_queue_t *const queue = channel_queue_of_role(channel, role);
	async_coroutine_t *const waiter = (async_coroutine_t *) ZEND_ASYNC_CURRENT_COROUTINE;

	if (UNEXPECTED(waiter == NULL)) {
		zend_throw_error(NULL, "There is no coroutine to suspend");
		return NULL;
	}

	async_wait_end(waiter);

	if (token != NULL && UNEXPECTED(!async_await_token_check(token))) {
		return NULL;
	}

	channel_queue_make_room(queue);

	if (token != NULL) {
		async_callbacks_reserve(async_awaitable_callbacks(token), 1);

		if (UNEXPECTED(!async_await_token_arm(token))) {
			return NULL;
		}
	}

	async_coroutine_event_callback_t *const record = &waiter->waker.records[0];

	async_wait_link_outside(record, waiter, (async_awaitable_t *) &channel->base, &channel_wait_kind);
	record->event_callback.flags |= role;
	channel_queue_push(queue, record);

	if (token != NULL) {
		async_await_token_link(&waiter->waker.records[1], waiter, token);
	}

	channel_timer_refresh(channel, true);

	/* The timer's submit failed: the wait ends before it parks. */
	if (UNEXPECTED(EG(exception) != NULL)) {
		async_wait_end(waiter);
		return NULL;
	}

	return record;
}

/* Parks the current coroutine in its role's queue (`role`: the record's SENDER, DELIVERING and
 * HOLDS_CHANNEL bits) until a value or a slot is promised to it, or a delivering sender until its value
 * is taken (channel.c:676-786).
 *
 * True only when it came back holding a reservation, which the caller spends at once: the counter is
 * given back here, so nothing may run in between. Otherwise an exception is pending, or something else
 * woke it: a waiter for a value or a slot tries again, and a delivering sender returns, as in
 * TrueAsync. */
static bool channel_wait(async_channel_t *channel, async_awaitable_t *const token, const uint32_t role)
{
	/* Starts as true: a delivering wait refused before it parks must still withdraw its value. */
	bool was_queued = true;
	bool had_reservation = false;

	async_coroutine_event_callback_t *const record = channel_wait_link(channel, token, role);

	if (EXPECTED(record != NULL)) {
		ZEND_ASYNC_SUSPEND();

		/* Taken out of the queue by the close or by the delivery's acknowledgement, else still there. */
		was_queued = channel_record_leave(record, &had_reservation);
	}

	const bool is_failed = EG(exception) != NULL;
	const bool is_undelivered = (role & CHANNEL_RECORD_F_DELIVERING) && was_queued;
	zval dropped;

	ZVAL_UNDEF(&dropped);

	/* A delivering sender still queued was not delivered: send() throws, so its value must not reach a
	 * receiver. One taken and then cancelled reports the cancellation (channel.c:745-752). */
	if (UNEXPECTED(is_undelivered && is_failed)) {
		channel_withdraw_rendezvous_value(channel, &dropped);
	}

	/* An unspent reservation, or the slot a withdrawn value frees, goes to the next waiter of the role; a
	 * closed channel has none left (channel.c:766-779). */
	const bool hands_on = is_failed && (is_undelivered || had_reservation);

	if (UNEXPECTED(hands_on) && !channel_is_closed(channel)) {
		if (role & CHANNEL_RECORD_F_SENDER) {
			channel_wake_sender(channel);
		} else {
			channel_wake_receiver(channel);
		}
	}

	channel_timer_refresh(channel, false);
	zval_ptr_dtor(&dropped);

	return had_reservation && !is_failed;
}

///////////////////////////////////////////////////////////////////
/// Sending and receiving
///////////////////////////////////////////////////////////////////

/* Puts `value` into a free or promised slot; false when a rendezvous value found no receiver and stays
 * uncommitted in the slot. */
static bool channel_put(async_channel_t *channel, const zval *value)
{
	if (channel_is_buffered(channel)) {
		zval_circular_buffer_push(&channel->buffer, value, channel->capacity);
		channel_wake_receiver(channel);
		return true;
	}

	ZEND_ASSERT(!channel->rendezvous_has_value && "a promised rendezvous slot is empty");

	ZVAL_COPY(&channel->rendezvous_value, value);
	channel->rendezvous_has_value = true;
	channel->rendezvous_committed = false;

	if (!channel_wake_receiver(channel)) {
		return false;
	}

	/* A woken receiver has not taken the value yet, and a close keeps it for that receiver; a Future took
	 * it already (channel.c:1146-1151). */
	channel->rendezvous_committed = channel->rendezvous_has_value;

	return true;
}

/* TrueAsync's send() loop (channel.c:1113-1172). */
static void channel_send(async_channel_t *channel, const zval *value, async_awaitable_t *const token)
{
	bool has_reservation = false;

	while (true) {
		if (UNEXPECTED(channel_is_closed(channel))) {
			channel_throw_closed(channel);
			return;
		}

		if (has_reservation || channel_has_free_slot(channel)) {
			if (!channel_put(channel, value)) {
				/* The value belongs to the channel now; this waits to hear that it was taken. */
				channel_wait(channel, token, CHANNEL_RECORD_F_SENDER | CHANNEL_RECORD_F_DELIVERING);
			}

			return;
		}

		has_reservation = channel_wait(channel, token, CHANNEL_RECORD_F_SENDER);

		if (UNEXPECTED(EG(exception) != NULL)) {
			return;
		}
	}
}

/* Whether the iterator lives only in a local of the C code that drives it, so that no slot the
 * collector's walk reads reports it: the engine's foreach between its rewind and storing the iterator
 * (zend_fe_reset_iterator()), and spl_iterator_apply() of iterator_to_array(), iterator_count() and
 * iterator_apply(). Its one reference to the channel then counts as the wait's: a step clears `current`
 * before it receives. */
static bool channel_iterator_is_c_local(void)
{
	const zend_execute_data *const frame = EG(current_execute_data);

	if (UNEXPECTED(frame == NULL || frame->func == NULL)) {
		return false;
	}

	if (EXPECTED(ZEND_USER_CODE(frame->func->type))) {
		return frame->opline->opcode == ZEND_FE_RESET_R;
	}

	const zend_string *const function_name = frame->func->common.function_name;

	return frame->func->common.scope == NULL && function_name != NULL &&
			(zend_string_equals_literal(function_name, "iterator_to_array") ||
			 zend_string_equals_literal(function_name, "iterator_count") ||
			 zend_string_equals_literal(function_name, "iterator_apply"));
}

/* TrueAsync's recv() loop (channel.c:1209-1245) and its iterator's (974-1012): false with nothing thrown
 * once the channel is closed and empty, false with an exception when the wait failed. `is_iterator`: an
 * iterator's step, whose iterator may hold the channel where the collector does not look. */
static bool
channel_receive(async_channel_t *channel, zval *result, async_awaitable_t *const token, const bool is_iterator)
{
	bool has_reservation = false;
	const uint32_t role = is_iterator && channel_iterator_is_c_local() ? CHANNEL_RECORD_F_HOLDS_CHANNEL : 0;

	while (true) {
		if (has_reservation || channel_has_free_value(channel)) {
			ZEND_ASSERT(channel_count(channel) > 0 && "a reservation outlived its value");

			channel_take_value(channel, result);
			channel_wake_sender(channel);
			return true;
		}

		if (UNEXPECTED(channel_is_closed(channel))) {
			return false;
		}

		has_reservation = channel_wait(channel, token, role);

		if (UNEXPECTED(EG(exception) != NULL)) {
			return false;
		}
	}
}

/* Sets `token` to the cancellation's awaitable with a reference for the call, or to NULL without a
 * cancellation. False with an exception when the object is no usable awaitable or has completed, as
 * TrueAsync's CANCELLATION_TOKEN_PREPARE (channel.c:57-61) refuses it before anything. */
static bool channel_token_acquire(zend_object *cancellation, async_awaitable_t **token)
{
	*token = NULL;

	if (cancellation == NULL) {
		return true;
	}

	async_awaitable_t *const awaitable = async_await_awaitable_of(cancellation);

	if (UNEXPECTED(awaitable == NULL || !async_await_token_check(awaitable))) {
		return false;
	}

	async_awaitable_addref(awaitable);
	*token = awaitable;

	return true;
}

static void channel_token_release(async_awaitable_t *const token)
{
	if (token != NULL) {
		async_awaitable_release(token);
	}
}

///////////////////////////////////////////////////////////////////
/// recvAsync() Futures
///////////////////////////////////////////////////////////////////

/* The Future completed or went: the waiter leaves the queue unless the channel took it out first or
 * went before it (channel.c:297-320). Unlike TrueAsync's, it takes a timer nobody starves for along, or
 * the timer would close an idle channel (Edmond, 2026-10-09). */
static void channel_future_waiter_dispose(async_event_callback_t *callback, async_awaitable_t *target)
{
	(void) target;

	channel_future_waiter_t *const waiter = CHANNEL_FUTURE_WAITER_OF(callback, on_future);
	async_channel_t *const channel = (async_channel_t *) waiter->queue_record.event;

	if (channel != NULL) {
		channel_queue_remove(&channel->receivers, &waiter->queue_record);
		channel_timer_disarm_if_idle(channel);
	}

	efree(waiter);
}

/* Queues a waiter for the pending `future`, which the next free value completes. */
static void channel_future_wait(async_channel_t *channel, async_future_event_t *future)
{
	channel_queue_make_room(&channel->receivers);
	async_callbacks_reserve(&future->base.callbacks, 1);

	channel_future_waiter_t *const waiter = emalloc(sizeof(channel_future_waiter_t));

	waiter->queue_record.event_callback.flags = 0;
	waiter->queue_record.event_callback.callback = NULL;
	waiter->queue_record.event_callback.kind = NULL;
	waiter->queue_record.coroutine = NULL;
	waiter->queue_record.event = (async_awaitable_t *) &channel->base;
	waiter->future = future;

	waiter->on_future.flags = 0;
	waiter->on_future.callback = async_callback_ignore;
	waiter->on_future.dispose = channel_future_waiter_dispose;

	async_callbacks_push_reserved(&future->base.callbacks, &waiter->on_future);
	channel_queue_push(&channel->receivers, &waiter->queue_record);
}

zend_object *async_channel_of_future_waiter(const async_event_callback_t *subscriber)
{
	/* A wait record's union holds its kind, not a dispose. */
	if (EXPECTED((subscriber->flags & ASYNC_CALLBACK_F_RECORD) ||
				 subscriber->dispose != channel_future_waiter_dispose)) {
		return NULL;
	}

	async_channel_t *const channel =
			(async_channel_t *) CHANNEL_FUTURE_WAITER_OF(subscriber, on_future)->queue_record.event;

	return channel != NULL ? &channel->std : NULL;
}

/* Detaches the waiters of Futures that outlive the channel: their dispose must not reach a freed queue.
 * They stay pending, as the channel's destructor did not run to reject them (after a fatal error;
 * S9-channel.md 8, item 8). */
static void channel_future_waiters_detach(async_channel_t *channel)
{
	for (uint32_t i = 0; i < channel->receivers.length; i++) {
		async_coroutine_event_callback_t *const record = channel->receivers.records[i];

		ZEND_ASSERT(channel_record_is_future(record) && "a parked coroutine holds its channel through its frame");
		record->event = NULL;
	}

	channel->receivers.length = 0;
}

///////////////////////////////////////////////////////////////////
/// The iterator
///////////////////////////////////////////////////////////////////

/* foreach over a channel receives as recv() (channel.c:943-1061); `data` holds the channel. */
typedef struct
{
	zend_object_iterator iterator;
	zval current; /* UNDEF before the first value and after the last */
	bool started;
} channel_iterator_t;

static void channel_iterator_dtor(zend_object_iterator *zend_iterator)
{
	channel_iterator_t *const iterator = (channel_iterator_t *) zend_iterator;

	zval_ptr_dtor(&iterator->current);
	zval_ptr_dtor(&zend_iterator->data);
}

static zend_result channel_iterator_valid(zend_object_iterator *zend_iterator)
{
	return Z_ISUNDEF(((channel_iterator_t *) zend_iterator)->current) ? FAILURE : SUCCESS;
}

static zval *channel_iterator_current(zend_object_iterator *zend_iterator)
{
	return &((channel_iterator_t *) zend_iterator)->current;
}

static void channel_iterator_key(zend_object_iterator *zend_iterator, zval *key)
{
	(void) zend_iterator;

	ZVAL_NULL(key);
}

/* Whether the loop ends quietly on the pending exception: an explicit close() is how a consumer is told
 * the values are over, while any other close and a cancellation the close found pending propagate. */
static bool channel_iterator_ends_quietly(const async_channel_t *channel)
{
	zend_object *const exception = EG(exception);

	if (exception->ce != async_ce_channel_exception || channel->close_reason != ASYNC_CHANNEL_CLOSE_EXPLICIT) {
		return false;
	}

	zval previous_storage;
	const zval *const previous =
			zend_read_property_ex(zend_ce_exception, exception, ZSTR_KNOWN(ZEND_STR_PREVIOUS), true, &previous_storage);

	return Z_TYPE_P(previous) == IS_NULL;
}

static void channel_iterator_move_forward(zend_object_iterator *zend_iterator)
{
	channel_iterator_t *const iterator = (channel_iterator_t *) zend_iterator;
	async_channel_t *const channel = channel_from_object(Z_OBJ(zend_iterator->data));

	/* Taken out before its release: a destructor that steps the same iterator, or suspends so that another
	 * coroutine's step runs meanwhile, would release it again. */
	zval previous_value;

	ZVAL_COPY_VALUE(&previous_value, &iterator->current);
	ZVAL_UNDEF(&iterator->current);
	zval_ptr_dtor(&previous_value);

	/* The previous value's destructor threw: a receive now would take a value the loop never sees. */
	if (UNEXPECTED(EG(exception) != NULL)) {
		return;
	}

	if (UNEXPECTED(async_throw_if_unavailable())) {
		return;
	}

	zval value;

	if (EXPECTED(channel_receive(channel, &value, NULL, true))) {
		/* The destructor's own step or another coroutine's step may have stored a value meanwhile; it is
		 * released once this value is in place, as above. */
		ZVAL_COPY_VALUE(&previous_value, &iterator->current);
		ZVAL_COPY_VALUE(&iterator->current, &value);
		zval_ptr_dtor(&previous_value);

		return;
	}

	if (EG(exception) != NULL && channel_iterator_ends_quietly(channel)) {
		zend_clear_exception();
	}
}

static HashTable *channel_iterator_gc(zend_object_iterator *zend_iterator, zval **table, int *num)
{
	channel_iterator_t *const iterator = (channel_iterator_t *) zend_iterator;
	zend_get_gc_buffer *const gc_buffer = zend_get_gc_buffer_create();

	zend_get_gc_buffer_add_zval(gc_buffer, &zend_iterator->data);
	zend_get_gc_buffer_add_zval(gc_buffer, &iterator->current);
	zend_get_gc_buffer_use(gc_buffer, table, num);

	return NULL;
}

/* Starts the loop once: a second foreach over the same iterator goes on where the first stopped. */
static void channel_iterator_rewind(zend_object_iterator *zend_iterator)
{
	channel_iterator_t *const iterator = (channel_iterator_t *) zend_iterator;

	if (!iterator->started) {
		iterator->started = true;
		channel_iterator_move_forward(zend_iterator);
	}
}

static const zend_object_iterator_funcs channel_iterator_funcs = {
	.dtor = channel_iterator_dtor,
	.valid = channel_iterator_valid,
	.get_current_data = channel_iterator_current,
	.get_current_key = channel_iterator_key,
	.move_forward = channel_iterator_move_forward,
	.rewind = channel_iterator_rewind,
	.get_gc = channel_iterator_gc,
};

static zend_object_iterator *channel_get_iterator(zend_class_entry *class_entry, zval *object, int by_ref)
{
	(void) class_entry;

	if (UNEXPECTED(by_ref)) {
		zend_throw_error(NULL, "Cannot iterate channel by reference");
		return NULL;
	}

	channel_iterator_t *const iterator = emalloc(sizeof(channel_iterator_t));

	zend_iterator_init(&iterator->iterator);
	iterator->iterator.funcs = &channel_iterator_funcs;
	ZVAL_OBJ_COPY(&iterator->iterator.data, Z_OBJ_P(object));
	ZVAL_UNDEF(&iterator->current);
	iterator->started = false;

	return &iterator->iterator;
}

///////////////////////////////////////////////////////////////////
/// Objects
///////////////////////////////////////////////////////////////////

static zend_object *channel_object_create(zend_class_entry *class_entry)
{
	async_channel_t *const channel = zend_object_alloc(sizeof(async_channel_t), class_entry);

	memset(channel, 0, offsetof(async_channel_t, std));
	async_event_init_in_object(&channel->base, 0, offsetof(async_channel_t, std));
	ZVAL_UNDEF(&channel->rendezvous_value);
	ZVAL_UNDEF(&channel->dropped_value);
	channel->timer_callback.callback = channel_timer_fire;
	channel->owner_scope_callback.callback = channel_owner_scope_notified;
	channel->owner_scope_callback.dispose = channel_owner_scope_dispose;

	zend_object_std_init(&channel->std, class_entry);
	object_properties_init(&channel->std, class_entry);

	return &channel->std;
}

/* The destructor closes with DISPOSED (channel.c:909-916): a parked waiter holds the channel through
 * its frame, so none is left to wake here. */
static void channel_object_destroy(zend_object *object)
{
	channel_close_and_release(channel_from_object(object), ASYNC_CHANNEL_CLOSE_DISPOSED);
}

/* zend_object_std_dtor first: it clears the WeakReferences, which a destructor of a value released here
 * would otherwise use to reach the channel being freed. */
static void channel_object_free(zend_object *object)
{
	async_channel_t *const channel = channel_from_object(object);

	zend_object_std_dtor(object);
	channel_unbind_from_owner_scope(channel);
	channel_timer_disarm(channel);
	channel_future_waiters_detach(channel);
	/* Nothing waits on the channel's event: its waiters sit in its queues. */
	ZEND_ASSERT(channel->base.callbacks.length == 0 && ASYNC_CALLBACKS_CAPACITY(&channel->base.callbacks) == 0);
	channel_queue_free(&channel->receivers);
	channel_queue_free(&channel->senders);
	zval_circular_buffer_dtor(&channel->buffer);
	zval_ptr_dtor(&channel->rendezvous_value);
	zval_ptr_dtor(&channel->dropped_value);
}

static HashTable *channel_object_gc(zend_object *object, zval **table, int *num)
{
	async_channel_t *const channel = channel_from_object(object);
	zend_get_gc_buffer *const gc_buffer = zend_get_gc_buffer_create();

	for (uint32_t i = 0; i < channel->buffer.count; i++) {
		zend_get_gc_buffer_add_zval(gc_buffer, zval_circular_buffer_at(&channel->buffer, i));
	}

	zend_get_gc_buffer_add_zval(gc_buffer, &channel->rendezvous_value);
	zend_get_gc_buffer_add_zval(gc_buffer, &channel->dropped_value);

	zend_get_gc_buffer_use(gc_buffer, table, num);

	return NULL;
}

///////////////////////////////////////////////////////////////////
/// Methods
///////////////////////////////////////////////////////////////////

ZEND_METHOD(Async_Channel, __construct)
{
	zend_long capacity = 0;
	zend_long no_producer_timeout = 0;
	zend_long no_consumer_timeout = 0;
	bool hard_timeouts = false;

	ZEND_PARSE_PARAMETERS_START(0, 4)
		Z_PARAM_OPTIONAL
		Z_PARAM_LONG(capacity)
		Z_PARAM_LONG(no_producer_timeout)
		Z_PARAM_LONG(no_consumer_timeout)
		Z_PARAM_BOOL(hard_timeouts)
	ZEND_PARSE_PARAMETERS_END();

	async_channel_t *const channel = THIS_CHANNEL;

	/* TrueAsync's second construction drops the held values and leaks them (S9-channel.md 8, item 4). */
	if (UNEXPECTED(channel->base.flags & ASYNC_CHANNEL_F_CONSTRUCTED)) {
		zend_throw_error(NULL, "Cannot call constructor twice");
		RETURN_THROWS();
	}

	const zend_long arguments[] = { capacity, no_producer_timeout, no_consumer_timeout };

	for (uint32_t i = 0; i < sizeof(arguments) / sizeof(arguments[0]); i++) {
		if (UNEXPECTED(arguments[i] < 0 || arguments[i] > INT32_MAX)) {
			zend_argument_value_error(i + 1, "must be between 0 and %d", INT32_MAX);
			RETURN_THROWS();
		}
	}

	channel->capacity = (uint32_t) capacity;
	channel->no_producer_timeout_ms = (int32_t) no_producer_timeout;
	channel->no_consumer_timeout_ms = (int32_t) no_consumer_timeout;
	channel->hard_timeouts = hard_timeouts;
	channel->base.flags |= ASYNC_CHANNEL_F_CONSTRUCTED;
	channel_bind_to_owner_scope(channel);
}

ZEND_METHOD(Async_Channel, send)
{
	zval *value;
	zend_object *cancellation = NULL;

	ZEND_PARSE_PARAMETERS_START(1, 2)
		Z_PARAM_ZVAL(value)
		Z_PARAM_OPTIONAL
		Z_PARAM_OBJ_OF_CLASS_OR_NULL(cancellation, async_ce_completable)
	ZEND_PARSE_PARAMETERS_END();

	THROW_IF_UNAVAILABLE();

	async_awaitable_t *token;

	if (UNEXPECTED(!channel_token_acquire(cancellation, &token))) {
		RETURN_THROWS();
	}

	channel_send(THIS_CHANNEL, value, token);
	channel_token_release(token);
}

ZEND_METHOD(Async_Channel, sendAsync)
{
	zval *value;

	ZEND_PARSE_PARAMETERS_START(1, 1)
		Z_PARAM_ZVAL(value)
	ZEND_PARSE_PARAMETERS_END();

	async_channel_t *const channel = THIS_CHANNEL;

	if (channel_is_closed(channel) || !channel_has_free_slot(channel)) {
		RETURN_FALSE;
	}

	/* An uncommitted rendezvous value stays in the slot for the next receiver. */
	(void) channel_put(channel, value);
	RETURN_TRUE;
}

ZEND_METHOD(Async_Channel, recv)
{
	zend_object *cancellation = NULL;

	ZEND_PARSE_PARAMETERS_START(0, 1)
		Z_PARAM_OPTIONAL
		Z_PARAM_OBJ_OF_CLASS_OR_NULL(cancellation, async_ce_completable)
	ZEND_PARSE_PARAMETERS_END();

	THROW_IF_UNAVAILABLE();

	async_awaitable_t *token;

	if (UNEXPECTED(!channel_token_acquire(cancellation, &token))) {
		RETURN_THROWS();
	}

	async_channel_t *const channel = THIS_CHANNEL;

	if (UNEXPECTED(!channel_receive(channel, return_value, token, false) && EG(exception) == NULL)) {
		channel_throw_closed(channel);
	}

	channel_token_release(token);
}

/* A Future of the next free value: completed at once with one, failed on a closed channel, else
 * pending in the receivers' queue (channel.c:1247-1292). */
ZEND_METHOD(Async_Channel, recvAsync)
{
	ZEND_PARSE_PARAMETERS_NONE();

	async_channel_t *const channel = THIS_CHANNEL;
	async_future_event_t *future;
	zend_object *const future_object = async_future_new_pending(&future);

	if (channel_has_free_value(channel)) {
		channel_future_give_value(channel, future);
		channel_wake_sender(channel);
	} else if (channel_is_closed(channel)) {
		channel_future_reject(future, channel->close_reason);
	} else {
		channel_future_wait(channel, future);
	}

	RETURN_OBJ(future_object);
}

ZEND_METHOD(Async_Channel, close)
{
	ZEND_PARSE_PARAMETERS_NONE();

	channel_close_and_release(THIS_CHANNEL, ASYNC_CHANNEL_CLOSE_EXPLICIT);
}

ZEND_METHOD(Async_Channel, isClosed)
{
	ZEND_PARSE_PARAMETERS_NONE();

	RETURN_BOOL(channel_is_closed(THIS_CHANNEL));
}

ZEND_METHOD(Async_Channel, capacity)
{
	ZEND_PARSE_PARAMETERS_NONE();

	RETURN_LONG(THIS_CHANNEL->capacity);
}

ZEND_METHOD(Async_Channel, count)
{
	ZEND_PARSE_PARAMETERS_NONE();

	RETURN_LONG(channel_count(THIS_CHANNEL));
}

ZEND_METHOD(Async_Channel, isEmpty)
{
	ZEND_PARSE_PARAMETERS_NONE();

	RETURN_BOOL(channel_count(THIS_CHANNEL) == 0);
}

ZEND_METHOD(Async_Channel, isFull)
{
	ZEND_PARSE_PARAMETERS_NONE();

	RETURN_BOOL(channel_free_space(THIS_CHANNEL) == 0);
}

/* An \Iterator over the same handler; TrueAsync's returns its raw wrapper and fails its own return
 * type (S9-channel.md 8, item 5). */
ZEND_METHOD(Async_Channel, getIterator)
{
	ZEND_PARSE_PARAMETERS_NONE();

	zend_create_internal_iterator_zval(return_value, ZEND_THIS);
}

void async_register_channel_ce(void)
{
	async_ce_channel_close_reason = register_class_Async_ChannelCloseReason();
	async_ce_channel_exception = register_class_Async_ChannelException(async_ce_async_exception);
	async_ce_channel = register_class_Async_Channel(async_ce_awaitable, zend_ce_aggregate, zend_ce_countable);
	async_ce_channel->create_object = channel_object_create;
	async_ce_channel->get_iterator = channel_get_iterator;
	async_ce_channel->default_object_handlers = &channel_handlers;

	memcpy(&channel_handlers, zend_get_std_object_handlers(), sizeof(zend_object_handlers));
	channel_handlers.offset = offsetof(async_channel_t, std);
	channel_handlers.dtor_obj = channel_object_destroy;
	channel_handlers.free_obj = channel_object_free;
	channel_handlers.get_gc = channel_object_gc;
	channel_handlers.clone_obj = NULL;
}
