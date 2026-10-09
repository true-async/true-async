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
#include "zend_exceptions.h"
#include "php_true_async.h"
#include "exceptions.h"
#include "timeout.h"
#include "timeout_arginfo.h"

zend_class_entry *async_ce_timeout = NULL;

static zend_object_handlers timeout_handlers;

typedef struct
{
	async_event_ref_t ref; /* one reference to the event */
	zend_object std;
} timeout_object_t;

static zend_always_inline timeout_object_t *timeout_object_from_object(zend_object *object)
{
	return (timeout_object_t *) ((char *) object - offsetof(timeout_object_t, std));
}

#define TIMEOUT_EVENT(object) ((async_timeout_event_t *) timeout_object_from_object(object)->ref.event)

///////////////////////////////////////////////////////////////////
/// The event
///////////////////////////////////////////////////////////////////

/* Until timeout() sets one: the latest finite deadline, as async_reactor_deadline_from_ms() saturates;
 * the Ring refuses an infinite one for a Timer. */
#define TIMEOUT_LATEST_DEADLINE (ZEND_HRTIME_T_MAX - 1)

static void timeout_set_deadline(async_timeout_event_t *timeout, const zend_long ms)
{
	timeout->ms = ms;
	timeout->deadline = async_reactor_deadline_from_ms(ms);
}

static async_timeout_event_t *timeout_event_new(void)
{
	async_timeout_event_t *timeout = emalloc(sizeof(async_timeout_event_t));

	async_event_init(&timeout->base, ASYNC_TIMEOUT_F_TIMEOUT);
	timeout->deadline.hrtime = TIMEOUT_LATEST_DEADLINE;
	timeout->ms = 0;
	timeout->subscriber_count = 0;
	timeout->timer = NULL;
	timeout->exception = NULL;

	return timeout;
}

void async_timeout_release(async_timeout_event_t *timeout)
{
	if (--timeout->base.ref_count > 0) {
		return;
	}

	ZEND_ASSERT(timeout->timer == NULL && "an armed timer has a subscriber, which holds the event");

	if (UNEXPECTED(timeout->exception != NULL)) {
		OBJ_RELEASE(timeout->exception);
	}

	async_callbacks_free((async_awaitable_t *) timeout, &timeout->base.callbacks);
	efree(timeout);
}

/* Withdraws the timer's op, if it is armed; a completed op withdraws as nothing. */
static void timeout_disarm(async_timeout_event_t *timeout)
{
	async_io_event_t *timer = timeout->timer;

	if (timer == NULL) {
		return;
	}

	timeout->timer = NULL;
	async_io_event_orphan(timer);
	async_callbacks_remove(&timer->base.callbacks, &timeout->timer_callback);
	async_io_event_release(timer);
}

/* Every completion: the timer's, a passed deadline found by a check, cancel(). Takes `exception`, the
 * argument of cancel(). The records take what they wake with from async_timeout_exception(). */
static void timeout_fire(async_timeout_event_t *timeout, zend_object *exception)
{
	ZEND_ASSERT(!(timeout->base.flags & ASYNC_EVENT_F_CLOSED));

	timeout->base.ref_count++;
	timeout_disarm(timeout);
	timeout->exception = exception;
	timeout->base.flags |= ASYNC_EVENT_F_CLOSED;
	async_callbacks_notify((async_awaitable_t *) timeout, &timeout->base.callbacks, NULL, exception);
	/* A completed Timeout takes no record, and one that a throwing subscriber left behind wakes here. */
	async_callbacks_free((async_awaitable_t *) timeout, &timeout->base.callbacks);
	async_timeout_release(timeout);
}

static void timeout_fire_by_deadline(async_timeout_event_t *timeout)
{
	timeout_fire(timeout, NULL);
}

/* TrueAsync's timeout_before_notify_handler (async.c:1622-1636) makes one per fire; one per wait here,
 * since the engine's chaining onto an exception thrown writes into its tail, and a shared one would
 * carry what one wait chained to every later one. */
zend_object *async_timeout_exception(const async_timeout_event_t *timeout)
{
	ZEND_ASSERT(timeout->base.flags & ASYNC_EVENT_F_CLOSED);

	if (UNEXPECTED(timeout->base.flags & ASYNC_TIMEOUT_F_CANCELLED)) {
		if (timeout->exception != NULL) {
			GC_ADDREF(timeout->exception);
		}

		return timeout->exception;
	}

	return async_new_exception(
			async_ce_timeout_exception, "Timeout occurred after " ZEND_LONG_FMT " milliseconds", timeout->ms);
}

bool async_timeout_fire_if_due(async_timeout_event_t *timeout)
{
	if (!(timeout->base.flags & ASYNC_EVENT_F_CLOSED) && UNEXPECTED(timeout->deadline.hrtime <= zend_hrtime())) {
		timeout_fire_by_deadline(timeout);
	}

	return (timeout->base.flags & ASYNC_EVENT_F_CLOSED) != 0;
}

/* The timer's completion, in its notify. */
static void
timer_callback_wake(async_awaitable_t *target, async_event_callback_t *callback, void *result, zend_object *exception)
{
	(void) target;
	(void) result;
	(void) exception;

	async_timeout_event_t *timeout =
			(async_timeout_event_t *) ((char *) callback - offsetof(async_timeout_event_t, timer_callback));

	timeout_fire_by_deadline(timeout);
}

/* async_reactor_delay()'s steps with the Timeout's callback in place of a record. The submit may
 * complete the op at once and fire the Timeout, which drops `timer`: a reference of the arm's own keeps
 * the IO event across it. */
static bool timeout_arm(async_timeout_event_t *timeout)
{
	async_io_event_t *timer = async_io_event_new();

	php_io_op_timer(&timer->op, timeout->deadline);
	async_callbacks_reserve(&timer->base.callbacks, 1);

	timeout->timer_callback.flags = 0;
	timeout->timer_callback.callback = timer_callback_wake;
	timeout->timer_callback.dispose = NULL;
	async_callbacks_push_reserved(&timer->base.callbacks, &timeout->timer_callback);

	timeout->timer = timer;
	timer->base.ref_count++;

	const zend_result submitted = async_io_event_submit(timer);

	if (UNEXPECTED(submitted == FAILURE)) {
		timeout->timer = NULL;
		async_callbacks_remove(&timer->base.callbacks, &timeout->timer_callback);
		async_io_event_release(timer);
	}

	async_io_event_release(timer);

	return submitted == SUCCESS;
}

bool async_timeout_subscribe(async_timeout_event_t *timeout)
{
	if (UNEXPECTED(timeout->base.flags & ASYNC_EVENT_F_CLOSED)) {
		return false;
	}

	/* A forked child's rebuild drops the parent's op from the reactor's lists; it runs lazily, so it
	 * goes first. */
	if (timeout->timer != NULL) {
		async_reactor_check_fork();

		/* The rebuild's cancels may unsubscribe the last holder, which disarms. */
		if (timeout->timer != NULL && UNEXPECTED(timeout->timer->reactor_link.prev == NULL)) {
			timeout_disarm(timeout);
		}
	}

	if (timeout->timer == NULL && UNEXPECTED(!timeout_arm(timeout))) {
		return false;
	}

	if (UNEXPECTED(timeout->base.flags & ASYNC_EVENT_F_CLOSED)) {
		return false;
	}

	timeout->subscriber_count++;
	timeout->base.ref_count++;

	return true;
}

void async_timeout_unsubscribe(async_timeout_event_t *timeout)
{
	ZEND_ASSERT(timeout->subscriber_count > 0);

	if (--timeout->subscriber_count == 0) {
		timeout_disarm(timeout);
	}

	async_timeout_release(timeout);
}

///////////////////////////////////////////////////////////////////
/// Async\timeout() and the object
///////////////////////////////////////////////////////////////////

/* async.c:777-797: the deadline is taken here, the timer armed by the first wait (D32). */
ZEND_FUNCTION(Async_timeout)
{
	zend_long ms;

	ZEND_PARSE_PARAMETERS_START(1, 1)
		Z_PARAM_LONG(ms)
	ZEND_PARSE_PARAMETERS_END();

	if (UNEXPECTED(ms <= 0)) {
		zend_value_error("Timeout value must be greater than 0");
		RETURN_THROWS();
	}

	object_init_ex(return_value, async_ce_timeout);
	timeout_set_deadline(TIMEOUT_EVENT(Z_OBJ_P(return_value)), ms);
}

static zend_object *timeout_object_create(zend_class_entry *class_entry)
{
	timeout_object_t *object = zend_object_alloc(sizeof(timeout_object_t), class_entry);

	object->ref.flags = ASYNC_EVENT_REFERENCE_PREFIX;
	object->ref.event = &timeout_event_new()->base;

	zend_object_std_init(&object->std, class_entry);
	object_properties_init(&object->std, class_entry);

	return &object->std;
}

/* zend_object_std_dtor first: it clears the WeakReferences, which a destructor of the released
 * cancellation would otherwise use to reach the Timeout being freed. */
static void timeout_object_free(zend_object *object)
{
	timeout_object_t *timeout_object = timeout_object_from_object(object);
	async_timeout_event_t *timeout = (async_timeout_event_t *) timeout_object->ref.event;

	zend_object_std_dtor(object);
	timeout_object->ref.event = NULL;
	async_timeout_release(timeout);
}

/* The cancel() argument, reported by the event's sole holder, as a future event's outcome is. */
static HashTable *timeout_object_gc(zend_object *object, zval **table, int *count)
{
	const async_timeout_event_t *timeout = TIMEOUT_EVENT(object);

	if (EXPECTED(timeout->base.ref_count != 1 || timeout->exception == NULL)) {
		*table = NULL;
		*count = 0;
		return NULL;
	}

	zend_get_gc_buffer *gc_buffer = zend_get_gc_buffer_create();

	zend_get_gc_buffer_add_obj(gc_buffer, timeout->exception);
	zend_get_gc_buffer_use(gc_buffer, table, count);

	return NULL;
}

ZEND_METHOD(Async_Timeout, __construct)
{
	zend_throw_error(NULL, "Timeout cannot be constructed directly");
}

/* D32 rule 7: terminal, and the waiters wake with OperationCanceledException whose previous is
 * `$cancellation`. A second call does nothing, as in TrueAsync. */
ZEND_METHOD(Async_Timeout, cancel)
{
	zend_object *cancellation = NULL;

	ZEND_PARSE_PARAMETERS_START(0, 1)
		Z_PARAM_OPTIONAL
		Z_PARAM_OBJ_OF_CLASS_OR_NULL(cancellation, async_ce_cancellation)
	ZEND_PARSE_PARAMETERS_END();

	async_timeout_event_t *timeout = TIMEOUT_EVENT(Z_OBJ_P(ZEND_THIS));

	if (UNEXPECTED(timeout->base.flags & ASYNC_EVENT_F_CLOSED)) {
		return;
	}

	if (cancellation != NULL) {
		GC_ADDREF(cancellation);
	}

	timeout->base.flags |= ASYNC_TIMEOUT_F_CANCELLED;
	timeout_fire(timeout, cancellation);
}

ZEND_METHOD(Async_Timeout, isCompleted)
{
	ZEND_PARSE_PARAMETERS_NONE();

	RETURN_BOOL(async_timeout_fire_if_due(TIMEOUT_EVENT(Z_OBJ_P(ZEND_THIS))));
}

ZEND_METHOD(Async_Timeout, isCancelled)
{
	ZEND_PARSE_PARAMETERS_NONE();

	RETURN_BOOL((TIMEOUT_EVENT(Z_OBJ_P(ZEND_THIS))->base.flags & ASYNC_TIMEOUT_F_CANCELLED) != 0);
}

void async_register_timeout_ce(zend_class_entry *completable_interface)
{
	async_ce_timeout = register_class_Async_Timeout(completable_interface);
	async_ce_timeout->create_object = timeout_object_create;
	async_ce_timeout->default_object_handlers = &timeout_handlers;

	memcpy(&timeout_handlers, zend_get_std_object_handlers(), sizeof(zend_object_handlers));
	timeout_handlers.offset = offsetof(timeout_object_t, std);
	timeout_handlers.free_obj = timeout_object_free;
	timeout_handlers.get_gc = timeout_object_gc;
	timeout_handlers.clone_obj = NULL;
}
