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
#include "scheduler.h"
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

#define CHANNEL_QUEUE_FIRST_CAPACITY 4

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
/// Wakes
///////////////////////////////////////////////////////////////////

/* The record stays linked for its frame. */
static void channel_record_wake(const async_coroutine_event_callback_t *record, zend_object *exception)
{
#ifdef TRUE_ASYNC_TEST_HOOKS
	async_collector_check_event_wake(record->coroutine);
#endif

	async_scheduler_enqueue(&record->coroutine->coroutine, exception, false);
}

/* Promises what was freed to the oldest record of `queue` without a reservation and wakes it; false
 * when there is no such record. The record stays queued, and its coroutine is woken even when a cancel
 * queued it already: its frame then hands the reservation on. */
static bool channel_promise_first(async_channel_queue_t *queue, uint32_t *reserved_count)
{
	async_coroutine_event_callback_t *const record = channel_queue_first_unreserved(queue);

	if (record == NULL) {
		return false;
	}

	record->event_callback.flags |= CHANNEL_RECORD_F_RESERVED;
	(*reserved_count)++;
	channel_record_wake(record, NULL);

	return true;
}

/* Promises a free value to the oldest receiver without a reservation; false when there is no such
 * value or receiver (channel.c:431-470). */
static bool channel_wake_receiver(async_channel_t *channel)
{
	return channel_has_free_value(channel) && channel_promise_first(&channel->receivers, &channel->reserved_receivers);
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
		channel_promise_first(&channel->senders, &channel->reserved_senders);
	}
}

/* Closes the channel once; the first reason stays (channel.c:521-586). Every waiter but a reserved
 * receiver leaves its queue and is woken with the close's exception: a reserved receiver's value is
 * still here and its recv() takes it. The walks go from the tail, so a removal never moves a record
 * they have yet to reach. An uncommitted rendezvous value moves to `dropped`, which the caller releases
 * once the channel is consistent: its destructor may use the channel. */
static void channel_close(async_channel_t *channel, const async_channel_close_reason_t reason, zval *dropped)
{
	if (channel_is_closed(channel)) {
		return;
	}

	channel->close_reason = reason;
	channel->base.flags |= ASYNC_EVENT_F_CLOSED;

	zend_object *const exception = channel_exception_new(reason);
	uint32_t index = channel->receivers.length;

	while (index > 0) {
		index--;
		async_coroutine_event_callback_t *const record = channel->receivers.records[index];

		if (record->event_callback.flags & CHANNEL_RECORD_F_RESERVED) {
			continue;
		}

		channel_queue_remove_at(&channel->receivers, index);
		channel_record_wake(record, exception);
	}

	index = channel->senders.length;

	while (index > 0) {
		index--;
		async_coroutine_event_callback_t *const record = channel->senders.records[index];

		channel_queue_remove_at(&channel->senders, index);
		channel_record_wake(record, exception);
	}

	OBJ_RELEASE(exception);
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
 * queued and no PHP code may run. The record leaves its queue and gives its reservation back without
 * waking anyone; a delivering sender's value stays in the slot for the next receiver. */
static void channel_record_abort(async_coroutine_event_callback_t *record)
{
	bool had_reservation;

	channel_record_leave(record, &had_reservation);
}

static const async_wait_kind_t channel_wait_kind = {
	.info = channel_record_info,
	.abort = channel_record_abort,
};

/* Links the current coroutine's record into its role's queue and its token record and returns the
 * queue's record; NULL with an exception, nothing linked, when there is no coroutine to park or the
 * token refuses. */
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

	return record;
}

/* Parks the current coroutine in its role's queue (`role`: the record's SENDER and DELIVERING bits)
 * until a value or a slot is promised to it, or a delivering sender until its value is taken
 * (channel.c:676-786).
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

	/* The woken receiver has not taken the value yet: a close keeps it for that receiver. */
	channel->rendezvous_committed = true;

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

/* TrueAsync's recv() loop (channel.c:1209-1245). */
static void channel_recv(async_channel_t *channel, zval *return_value, async_awaitable_t *const token)
{
	bool has_reservation = false;

	while (true) {
		if (has_reservation || channel_has_free_value(channel)) {
			ZEND_ASSERT(channel_count(channel) > 0 && "a reservation outlived its value");

			channel_take_value(channel, return_value);
			channel_wake_sender(channel);
			return;
		}

		if (UNEXPECTED(channel_is_closed(channel))) {
			channel_throw_closed(channel);
			return;
		}

		has_reservation = channel_wait(channel, token, 0);

		if (UNEXPECTED(EG(exception) != NULL)) {
			return;
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
/// Objects
///////////////////////////////////////////////////////////////////

static zend_object *channel_object_create(zend_class_entry *class_entry)
{
	async_channel_t *const channel = zend_object_alloc(sizeof(async_channel_t), class_entry);

	memset(channel, 0, offsetof(async_channel_t, std));
	async_event_init_in_object(&channel->base, ASYNC_CHANNEL_F_CHANNEL, offsetof(async_channel_t, std));
	ZVAL_UNDEF(&channel->rendezvous_value);

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

static void channel_object_free(zend_object *object)
{
	async_channel_t *const channel = channel_from_object(object);

	channel_queue_free(&channel->receivers);
	channel_queue_free(&channel->senders);
	zval_circular_buffer_dtor(&channel->buffer);
	zval_ptr_dtor(&channel->rendezvous_value);

	zend_object_std_dtor(object);
}

static HashTable *channel_object_gc(zend_object *object, zval **table, int *num)
{
	async_channel_t *const channel = channel_from_object(object);
	zend_get_gc_buffer *const gc_buffer = zend_get_gc_buffer_create();

	for (uint32_t i = 0; i < channel->buffer.count; i++) {
		zend_get_gc_buffer_add_zval(gc_buffer, zval_circular_buffer_at(&channel->buffer, i));
	}

	zend_get_gc_buffer_add_zval(gc_buffer, &channel->rendezvous_value);
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

	channel_recv(THIS_CHANNEL, return_value, token);
	channel_token_release(token);
}

ZEND_METHOD(Async_Channel, recvAsync)
{
	ZEND_PARSE_PARAMETERS_NONE();

	zend_throw_error(NULL, "Async\\Channel::recvAsync() is not implemented yet");
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

ZEND_METHOD(Async_Channel, getIterator)
{
	ZEND_PARSE_PARAMETERS_NONE();

	zend_throw_error(NULL, "Async\\Channel::getIterator() is not implemented yet");
}

void async_register_channel_ce(void)
{
	async_ce_channel_close_reason = register_class_Async_ChannelCloseReason();
	async_ce_channel_exception = register_class_Async_ChannelException(async_ce_async_exception);
	async_ce_channel = register_class_Async_Channel(async_ce_awaitable, zend_ce_aggregate, zend_ce_countable);
	async_ce_channel->create_object = channel_object_create;
	async_ce_channel->default_object_handlers = &channel_handlers;

	memcpy(&channel_handlers, zend_get_std_object_handlers(), sizeof(zend_object_handlers));
	channel_handlers.offset = offsetof(async_channel_t, std);
	channel_handlers.dtor_obj = channel_object_destroy;
	channel_handlers.free_obj = channel_object_free;
	channel_handlers.get_gc = channel_object_gc;
	channel_handlers.clone_obj = NULL;
}
