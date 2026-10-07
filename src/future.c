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
#include "php_true_async.h"
#include "Zend/zend_exceptions.h"
#include "await.h"
#include "collector.h"
#include "coroutine.h"
#include "exceptions.h"
#include "future.h"
#include "scheduler.h"
#include "scope.h"
#include "future_arginfo.h"

zend_class_entry *async_ce_future_state = NULL;
zend_class_entry *async_ce_future = NULL;

static zend_object_handlers future_state_handlers;
static zend_object_handlers future_handlers;

typedef enum
{
	FUTURE_MAPPER_MAP,
	FUTURE_MAPPER_CATCH,
	FUTURE_MAPPER_FINALLY,
} future_mapper_kind_t;

/* The writing end. It holds the event it made, and is the event's only object holder: a Future on
 * it holds the state, not the event (dev/plans/S5.md, section 2). */
typedef struct
{
	async_event_ref_t ref; /* one reference to the event */
	zend_object std;
} future_state_t;

/* The reading end. On a FutureState it holds the state object and reads the state's event through
 * `ref`; a child of map(), catch() or finally() and a Future of completed() or failed() hold their
 * event themselves. */
typedef struct
{
	async_event_ref_t ref; /* NULL event: never constructed (unserialize()) */
	zend_object *state;    /* the FutureState held, or NULL */
	zval mapper;           /* the callable of a child; UNDEF otherwise */
	future_mapper_kind_t mapper_kind;
	zend_object std;
} future_t;

#define FUTURE_EVENT(object) ((async_future_event_t *) (object)->ref.event)

static zend_always_inline future_state_t *future_state_from_object(zend_object *object)
{
	return (future_state_t *) ((char *) object - offsetof(future_state_t, std));
}

static zend_always_inline future_t *future_from_object(zend_object *object)
{
	return (future_t *) ((char *) object - offsetof(future_t, std));
}

#define THIS_FUTURE_STATE future_state_from_object(Z_OBJ_P(ZEND_THIS))
#define THIS_FUTURE future_from_object(Z_OBJ_P(ZEND_THIS))

///////////////////////////////////////////////////////////////////
/// The future event
///////////////////////////////////////////////////////////////////

/* The innermost user frame's file and line; NULL and 0 outside PHP code. */
static void future_location_set(zend_string **filename, uint32_t *lineno)
{
	zend_string *executed_filename = zend_get_executed_filename_ex();

	*filename = executed_filename != NULL ? zend_string_copy(executed_filename) : NULL;
	*lineno = zend_get_executed_lineno();
}

static async_future_event_t *future_event_new(void)
{
	async_future_event_t *future = emalloc(sizeof(async_future_event_t));

	async_event_init(&future->base, 0);
	ZVAL_UNDEF(&future->result);
	future->exception = NULL;
	future->completed_lineno = 0;
	future->completed_filename = NULL;
	memset(&future->chain, 0, sizeof(future->chain));
	future_location_set(&future->created_filename, &future->created_lineno);

	return future;
}

/* What nobody observed, with TrueAsync's texts and level (future.c:581-654). */
static void future_event_report_unobserved(const async_future_event_t *future)
{
	const uint32_t flags = future->base.flags;

	if (flags & ASYNC_FUTURE_F_IGNORED) {
		return;
	}

	if (!(flags & ASYNC_EVENT_F_RESULT_USED)) {
		if (future->created_filename != NULL) {
			zend_error(E_CORE_WARNING,
					   "Future was never used; call await(), map(), catch(), finally() or ignore() to suppress this "
					   "warning. Created at %s:%u",
					   ZSTR_VAL(future->created_filename),
					   future->created_lineno);
		} else {
			zend_error(E_CORE_WARNING,
					   "Future was never used; call await(), map(), catch(), finally() or ignore() to suppress this "
					   "warning");
		}
	}

	zend_object *exception = future->exception;

	if (exception == NULL || (flags & ASYNC_EVENT_F_EXC_CAUGHT)) {
		return;
	}

	zval message_holder;
	const zval *message = zend_read_property_ex(
			zend_get_exception_base(exception), exception, ZSTR_KNOWN(ZEND_STR_MESSAGE), true, &message_holder);
	const char *message_text = Z_TYPE_P(message) == IS_STRING ? Z_STRVAL_P(message) : "Unknown error";

	if (future->created_filename != NULL && future->completed_filename != NULL) {
		zend_error(E_CORE_WARNING,
				   "Unhandled exception in Future: %s; use catch() or ignore() to handle. Created at %s:%u, completed "
				   "at %s:%u",
				   message_text,
				   ZSTR_VAL(future->created_filename),
				   future->created_lineno,
				   ZSTR_VAL(future->completed_filename),
				   future->completed_lineno);
	} else if (future->created_filename != NULL) {
		zend_error(E_CORE_WARNING,
				   "Unhandled exception in Future: %s; use catch() or ignore() to handle. Created at %s:%u",
				   message_text,
				   ZSTR_VAL(future->created_filename),
				   future->created_lineno);
	} else {
		zend_error(
				E_CORE_WARNING, "Unhandled exception in Future: %s; use catch() or ignore() to handle", message_text);
	}

	/* A property hook on the message returns a value of its own. */
	if (UNEXPECTED(message == &message_holder)) {
		zval_ptr_dtor(&message_holder);
	}
}

static void future_chain_reserve(async_future_chain_t *chain, const uint32_t count)
{
	if (EXPECTED(chain->length + count <= chain->capacity)) {
		return;
	}

	chain->capacity = MAX(chain->capacity * 2, chain->length + count);
	chain->children = safe_erealloc(chain->children, chain->capacity, sizeof(zend_object *), 0);
}

/* Releases the children of a pending source that dies, its chain emptied first: the releases may run
 * PHP code. A child that dies with them hands its own children over before its release, so a chain of
 * any depth is released in this loop instead of by nested object frees. */
static void future_chain_free(async_future_chain_t *chain)
{
	async_future_chain_t children = *chain;

	memset(chain, 0, sizeof(*chain));

	for (uint32_t i = 0; i < children.length; i++) {
		zend_object *child_object = children.children[i];
		const future_t *child = future_from_object(child_object);
		async_future_event_t *child_event = FUTURE_EVENT(child);

		if (GC_REFCOUNT(child_object) == 1 && child->state == NULL && child_event != NULL &&
			child_event->base.ref_count == 1 && child_event->chain.length != 0) {
			future_chain_reserve(&children, child_event->chain.length);
			memcpy(children.children + children.length,
				   child_event->chain.children,
				   child_event->chain.length * sizeof(zend_object *));
			children.length += child_event->chain.length;
			efree(child_event->chain.children);
			memset(&child_event->chain, 0, sizeof(child_event->chain));
		}

		OBJ_RELEASE(child_object);
	}

	if (children.children != NULL) {
		efree(children.children);
	}
}

void async_future_event_release(async_future_event_t *future)
{
	if (--future->base.ref_count > 0) {
		return;
	}

	future_event_report_unobserved(future);

	zval_ptr_dtor(&future->result);

	if (future->exception != NULL) {
		OBJ_RELEASE(future->exception);
	}

	if (future->created_filename != NULL) {
		zend_string_release(future->created_filename);
	}

	if (future->completed_filename != NULL) {
		zend_string_release(future->completed_filename);
	}

	future_chain_free(&future->chain);
	async_callbacks_free((async_awaitable_t *) future, &future->base.callbacks);
	efree(future);
}

/* The event's contents, reported by its only holder: the collector subtracts one reference per
 * reported edge, so an event that two live holders reported could lose a result one of them still
 * has (dev/plans/S5.md, section 2). A waiter or a drain raises the count, and nothing is reported
 * while it runs. */
static void future_event_gc(async_future_event_t *future, zend_get_gc_buffer *gc_buffer)
{
	if (future == NULL || future->base.ref_count != 1) {
		return;
	}

	zend_get_gc_buffer_add_zval(gc_buffer, &future->result);

	if (future->exception != NULL) {
		zend_get_gc_buffer_add_obj(gc_buffer, future->exception);
	}

	for (uint32_t i = 0; i < future->chain.length; i++) {
		zend_get_gc_buffer_add_obj(gc_buffer, future->chain.children[i]);
	}
}

/* The event as a node of the collector of coroutines that can never wake (src/collector.c): it counts
 * its holders in ref_count, so each holder reports the event and the event reports its contents
 * once, whatever holds it. */
static void future_event_collector_references(async_event_t *event, async_collector_t *collector)
{
	async_future_event_t *future = (async_future_event_t *) event;

	async_collector_report_zval(collector, &future->result);

	if (UNEXPECTED(future->exception != NULL)) {
		async_collector_report_object(collector, future->exception);
	}

	for (uint32_t i = 0; i < future->chain.length; i++) {
		async_collector_report_object(collector, future->chain.children[i]);
	}
}

void async_future_collector_live(async_collector_t *collector, async_future_event_t *future)
{
	async_collector_report_live_event(collector, &future->base, future_event_collector_references);
}

void async_future_collector_target(async_collector_t *collector, async_future_event_t *future)
{
	async_collector_report_event_target(collector, &future->base, future_event_collector_references, true);
}

///////////////////////////////////////////////////////////////////
/// The drain: mappers in coroutines
///////////////////////////////////////////////////////////////////

typedef struct
{
	async_future_event_t *parent; /* completed; one reference */
	zend_object *child;           /* a child Future of it; one reference */
} future_drain_item_t;

/* The mappers of completed sources, called in a drain coroutine and the helpers it spawns
 * (dev/plans/S5.md, section 3), as TrueAsync's chain iterator calls them (future.c:205-323,
 * iterator.c:137-157): a child completed by a mapper queues its own children here, so a chain of
 * any depth runs breadth-first with no recursion. */
typedef struct
{
	/* First: the core's release frees the drain through it. Its ref_count counts the coroutines in the
	 * drain and the queued microtask, whose tick spawns one helper while items remain: a mapper that
	 * suspends does not hold back the items behind it. */
	zend_async_microtask_t microtask;
	future_drain_item_t *items; /* a FIFO from `head` to `length` */
	uint32_t head;
	uint32_t length;
	uint32_t capacity;
	bool armed;   /* the microtask is queued */
	bool stopped; /* an exit() in a mapper: no more items run; the rest go with the drain */
} future_drain_t;

static void
future_event_complete(async_future_event_t *future, zval *result, zend_object *exception, future_drain_t *drain);

static void future_drain_reserve(future_drain_t *drain, const uint32_t count)
{
	if (EXPECTED(drain->length + count <= drain->capacity)) {
		return;
	}

	if (drain->head != 0) {
		drain->length -= drain->head;
		memmove(drain->items, drain->items + drain->head, drain->length * sizeof(future_drain_item_t));
		drain->head = 0;

		if (drain->length + count <= drain->capacity) {
			return;
		}
	}

	drain->capacity = MAX(drain->capacity * 2, drain->length + count);
	drain->items = safe_erealloc(drain->items, drain->capacity, sizeof(future_drain_item_t), 0);
}

/* Takes a reference to `parent` and to `child`; room reserved before. */
static zend_always_inline void
future_drain_push(future_drain_t *drain, async_future_event_t *parent, zend_object *child)
{
	parent->base.ref_count++;
	drain->items[drain->length++] = (future_drain_item_t){ parent, child };
}

/* TrueAsync's process_future_mapper (future.c:1404-1526). map() runs on a result and passes an error
 * on; catch() runs on an error and observes it; finally() always runs and passes the outcome on.
 * What the callable throws rejects the child, over the error passed on as its previous; the child is
 * the only place an error goes (dev/plans/S5.md, section 8, item 1). */
static void future_mapper_run(future_drain_t *drain,
							  async_future_event_t *parent,
							  future_t *child,
							  async_future_event_t *child_event)
{
	/* A cancel() completed it while it waited in the drain. */
	if (UNEXPECTED(child_event->base.flags & ASYNC_EVENT_F_CLOSED)) {
		return;
	}

	zend_object *error = parent->exception;
	zval argument;
	bool should_call = true;

	ZVAL_NULL(&argument);

	if (EXPECTED(error == NULL)) {
		if (!Z_ISUNDEF(parent->result)) {
			ZVAL_COPY(&argument, &parent->result);
		}
	} else {
		GC_ADDREF(error);
	}

	switch (child->mapper_kind) {
		case FUTURE_MAPPER_MAP:
			should_call = error == NULL;
			break;

		case FUTURE_MAPPER_CATCH:
			if (error == NULL) {
				should_call = false;
				break;
			}

			ZVAL_OBJ(&argument, error);
			error = NULL;
			parent->base.flags |= ASYNC_EVENT_F_EXC_CAUGHT | ASYNC_EVENT_F_EXCEPTION_HANDLED;
			break;

		case FUTURE_MAPPER_FINALLY:
			if (error != NULL) {
				ZVAL_OBJ_COPY(&argument, error);
			}

			break;
	}

	zval retval;
	ZVAL_UNDEF(&retval);

	if (should_call) {
		call_user_function(NULL, NULL, &child->mapper, &retval, 1, &argument);
	}

	zval_ptr_dtor(&argument);

	if (child->mapper_kind == FUTURE_MAPPER_FINALLY) {
		zval_ptr_dtor(&retval);
		ZVAL_UNDEF(&retval);
	}

	if (UNEXPECTED(EG(exception) != NULL)) {
		if (UNEXPECTED(async_is_exit_object(EG(exception)))) {
			zval_ptr_dtor(&retval);

			if (error != NULL) {
				OBJ_RELEASE(error);
			}

			return;
		}

		zend_object *thrown = EG(exception);
		GC_ADDREF(thrown);
		zend_clear_exception();

		if (error != NULL) {
			zend_exception_set_previous(thrown, error);
		}

		error = thrown;
	}

	/* The callable may have cancelled its own child. */
	if (EXPECTED(!(child_event->base.flags & ASYNC_EVENT_F_CLOSED))) {
		if (error != NULL) {
			future_event_complete(child_event, NULL, error, drain);
		} else {
			if (Z_ISUNDEF(retval) && !Z_ISUNDEF(parent->result)) {
				ZVAL_COPY(&retval, &parent->result);
			} else if (Z_ISUNDEF(retval)) {
				ZVAL_NULL(&retval);
			}

			future_event_complete(child_event, &retval, NULL, drain);
		}
	}

	if (error != NULL) {
		OBJ_RELEASE(error);
	}

	zval_ptr_dtor(&retval);

	if (child_event->base.flags & ASYNC_EVENT_F_EXC_CAUGHT) {
		parent->base.flags |= ASYNC_EVENT_F_EXC_CAUGHT;
	}
}

static void future_drain_arm(future_drain_t *drain)
{
	ZEND_ASYNC_MICROTASK_ADDREF(&drain->microtask);

	if (UNEXPECTED(!ZEND_ASYNC_DEFER(&drain->microtask))) {
		drain->microtask.ref_count--;
		return;
	}

	drain->armed = true;
}

/* Runs items until the FIFO is empty; the items a mapper adds included. What a release throws (a
 * destructor) does not stop the drain, whose children would never complete: it waits aside and
 * becomes the coroutine's outcome. An exit() stops every coroutine of the drain. */
static void future_drain_run(future_drain_t *drain)
{
	zend_object *saved_exception = NULL;

	while (EXPECTED(!drain->stopped) && drain->head < drain->length) {
		const future_drain_item_t item = drain->items[drain->head++];

		if (drain->head == drain->length) {
			drain->head = 0;
			drain->length = 0;
		}

		/* Armed for the items behind this one at the time of each call, not once per coroutine: a
		 * resumed mapper's completion may queue items after the last tick found the FIFO empty. */
		if (drain->head < drain->length && !drain->armed) {
			future_drain_arm(drain);
		}

		future_t *child = future_from_object(item.child);
		/* The mapper may __construct() its child again, which lets go of this event. */
		async_future_event_t *child_event = FUTURE_EVENT(child);
		child_event->base.ref_count++;

		future_mapper_run(drain, item.parent, child, child_event);
		async_future_event_release(child_event);
		OBJ_RELEASE(item.child);
		async_future_event_release(item.parent);

		if (UNEXPECTED(EG(exception) != NULL)) {
			if (UNEXPECTED(async_is_exit_object(EG(exception)))) {
				drain->stopped = true;
				break;
			}

			async_exception_save_fast(&EG(exception), &saved_exception);
		}
	}

	/* An exit() is no exception to chain under. */
	if (UNEXPECTED(saved_exception != NULL && EG(exception) != NULL)) {
		OBJ_RELEASE(saved_exception);
		return;
	}

	async_exception_restore_fast(&EG(exception), &saved_exception);
}

/* The drain lives while its coroutine object does: a coroutine that never ran (cancelled at the
 * request's end) and one cut short by a bailout release it here too. */
static void future_drain_coroutine_dispose(zend_coroutine_t *coroutine)
{
	future_drain_t *drain = coroutine->extended_data;
	coroutine->extended_data = NULL;

	ZEND_ASYNC_MICROTASK_RELEASE(&drain->microtask);
}

static void future_drain_coroutine_entry(void)
{
	future_drain_run(ZEND_ASYNC_CURRENT_COROUTINE->extended_data);
}

/* A coroutine in the drain, with its own reference. False with an exception when the scheduler
 * refuses it. */
static bool future_drain_spawn(future_drain_t *drain)
{
	async_coroutine_t *coroutine = async_coroutine_new();

	coroutine->coroutine.internal_entry = future_drain_coroutine_entry;
	coroutine->coroutine.extended_data = drain;
	coroutine->coroutine.extended_dispose = future_drain_coroutine_dispose;
	ZEND_ASYNC_MICROTASK_ADDREF(&drain->microtask);
	/* A drain serves the chains of every scope (S9-scope.md section 3). */
	async_scope_add_coroutine(ASYNC_G(global_scope), coroutine);

	if (UNEXPECTED(!async_scheduler_enqueue(&coroutine->coroutine, NULL, false))) {
		async_scope_remove_coroutine(coroutine);
		zend_hash_index_del(&ASYNC_G(coroutines), coroutine->std.handle);
		OBJ_RELEASE(&coroutine->std);
		return false;
	}

	return true;
}

/* The tick after a mapper call that left items behind it: one more coroutine while items remain. */
static void future_drain_microtask(zend_async_microtask_t *microtask)
{
	future_drain_t *drain = (future_drain_t *) microtask;

	drain->armed = false;

	if (EXPECTED(!drain->stopped) && drain->head < drain->length) {
		future_drain_spawn(drain);
	}
}

/* Items left by a drain that never ran or stopped early. */
static void future_drain_dtor(zend_async_microtask_t *microtask)
{
	future_drain_t *drain = (future_drain_t *) microtask;

	for (uint32_t i = drain->head; i < drain->length; i++) {
		OBJ_RELEASE(drain->items[i].child);
		async_future_event_release(drain->items[i].parent);
	}

	if (drain->items != NULL) {
		efree(drain->items);
	}
}

static future_drain_t *future_drain_new(const uint32_t capacity)
{
	future_drain_t *drain = ecalloc(1, sizeof(future_drain_t));

	drain->microtask.handler = future_drain_microtask;
	drain->microtask.dtor = future_drain_dtor;
	drain->microtask.ref_count = 1;
	drain->items = safe_emalloc(capacity, sizeof(future_drain_item_t), 0);
	drain->capacity = capacity;

	return drain;
}

/* Starts the drain coroutine and drops the creator's reference. */
static void future_drain_start(future_drain_t *drain)
{
	future_drain_spawn(drain);
	ZEND_ASYNC_MICROTASK_RELEASE(&drain->microtask);
}

/* Moves the children of a completed source into `drain`, or into a new drain. */
static void future_chain_to_drain(async_future_event_t *future, future_drain_t *drain)
{
	const async_future_chain_t chain = future->chain;
	future_drain_t *new_drain = NULL;

	memset(&future->chain, 0, sizeof(future->chain));

	if (drain == NULL) {
		new_drain = future_drain_new(chain.length);
		drain = new_drain;
	} else {
		future_drain_reserve(drain, chain.length);
	}

	for (uint32_t i = 0; i < chain.length; i++) {
		future_drain_push(drain, future, chain.children[i]);
	}

	efree(chain.children);

	if (new_drain != NULL) {
		future_drain_start(new_drain);
	}
}

///////////////////////////////////////////////////////////////////
/// Completion and waiting
///////////////////////////////////////////////////////////////////

/* Completes a pending future with `result` or `exception` (borrowed), as TrueAsync's
 * zend_future_resolve (future.c:481-512): the waiters wake, and the children go to `drain`, the
 * drain whose mapper completes it, or to a new drain. The caller holds a reference to `future`. */
static void
future_event_complete(async_future_event_t *future, zval *result, zend_object *exception, future_drain_t *drain)
{
	ZEND_ASSERT(!(future->base.flags & ASYNC_EVENT_F_CLOSED) && "a future completes once");

	if (exception != NULL) {
		GC_ADDREF(exception);
		future->exception = exception;
	} else {
		ZVAL_COPY_DEREF(&future->result, result);
	}

	future_location_set(&future->completed_filename, &future->completed_lineno);
	future->base.flags |= ASYNC_EVENT_F_CLOSED;

	async_callbacks_notify((async_awaitable_t *) future, &future->base.callbacks, &future->result, future->exception);
	/* A completed future takes no record, and one that a throwing callback left behind wakes here and
	 * reads the outcome. */
	async_callbacks_free((async_awaitable_t *) future, &future->base.callbacks);

	if (future->chain.length != 0) {
		/* An outcome passed on to the children of a child counts as observed (TrueAsync's append to
		 * a running iterator, future.c:1575-1582). */
		if (drain != NULL) {
			future->base.flags |= ASYNC_EVENT_F_EXCEPTION_HANDLED;
		}

		future_chain_to_drain(future, drain);
	}

	if (future->base.flags & ASYNC_EVENT_F_EXCEPTION_HANDLED) {
		future->base.flags |= ASYNC_EVENT_F_EXC_CAUGHT;
	}
}

static zend_string *future_record_info(const async_coroutine_event_callback_t *record)
{
	(void) record;

	return zend_string_init(ZEND_STRL("await: future"), false);
}

/* The wait's reference to the event, taken in async_future_await(), sits in no slot the walk reads. */
static void future_record_collector_target(const async_coroutine_event_callback_t *record, async_collector_t *collector)
{
	async_future_collector_target(collector, (async_future_event_t *) record->event);
}

/* A wait for a future: its outcome is in the event, so nothing but the vector to leave. */
static const async_wait_kind_t async_wait_kind_future = {
	.info = future_record_info,
	.collector_target = future_record_collector_target,
};

/* The waiter reads the outcome from the event. A wake with an error marks it handled, as TrueAsync's
 * zend_async_waker_callback_resolve does (F zend_async_API.c:1261-1267); the completion turns that
 * into EXC_CAUGHT. */
static void
future_record_wake(async_awaitable_t *target, async_event_callback_t *callback, void *result, zend_object *exception)
{
	(void) result;

	if (exception != NULL) {
		((async_event_t *) target)->flags |= ASYNC_EVENT_F_EXCEPTION_HANDLED;
	}

	async_coroutine_event_callback_t *record = (async_coroutine_event_callback_t *) callback;

#ifdef TRUE_ASYNC_TEST_HOOKS
	async_collector_check_event_wake(record->coroutine);
#endif

	async_scheduler_enqueue(&record->coroutine->coroutine, NULL, false);
}

static bool future_event_take_outcome(async_future_event_t *future, zval *return_value)
{
	if (UNEXPECTED(future->exception != NULL)) {
		GC_ADDREF(future->exception);
		zend_throw_exception_internal(future->exception);
		return false;
	}

	if (Z_ISUNDEF(future->result)) {
		ZVAL_NULL(return_value);
	} else {
		ZVAL_COPY_DEREF(return_value, &future->result);
	}

	return true;
}

bool async_future_await(async_future_event_t *future, zval *return_value, async_awaitable_t *token)
{
	if (future->base.flags & ASYNC_EVENT_F_CLOSED) {
		return future_event_take_outcome(future, return_value);
	}

	async_coroutine_t *waiter = (async_coroutine_t *) ZEND_ASYNC_CURRENT_COROUTINE;

	/* A finished coroutine is still current while finalize releases what it held (a destructor). */
	if (UNEXPECTED(waiter == NULL || ZEND_COROUTINE_IS_FINISHED(&waiter->coroutine))) {
		zend_throw_error(NULL, "await() requires a running coroutine");
		return false;
	}

	/* A bailout that a shutdown function's zend_try caught can leave main's wait linked. */
	async_wait_end(waiter);

	/* The wait's own reference: the Future the waiter's frame holds may let go of the event
	 * (a second __construct()). */
	future->base.ref_count++;

	/* Another enqueue than the completion (a foreign one) wakes the waiter early: it waits again, as
	 * async_await_coroutine() does. */
	do {
		if (token != NULL && UNEXPECTED(!async_await_token_check(token))) {
			async_future_event_release(future);
			return false;
		}

		async_callbacks_reserve(&future->base.callbacks, 1);

		if (token != NULL) {
			async_callbacks_reserve(async_awaitable_callbacks(token), 1);

			if (UNEXPECTED(!async_await_token_arm(token))) {
				async_future_event_release(future);
				return false;
			}
		}

		async_wait_link(&waiter->waker.records[0],
						waiter,
						(async_awaitable_t *) future,
						&async_wait_kind_future,
						future_record_wake);

		if (token != NULL) {
			async_await_token_link(&waiter->waker.records[1], waiter, token);
		}

		if (UNEXPECTED(!ZEND_ASYNC_SUSPEND())) {
			async_future_event_release(future);
			return false;
		}
	} while (!(future->base.flags & ASYNC_EVENT_F_CLOSED));

	const bool taken = future_event_take_outcome(future, return_value);
	async_future_event_release(future);

	return taken;
}

///////////////////////////////////////////////////////////////////
/// Objects
///////////////////////////////////////////////////////////////////

static zend_object *future_state_object_create(zend_class_entry *class_entry)
{
	future_state_t *state = zend_object_alloc(sizeof(future_state_t), class_entry);

	state->ref.flags = ASYNC_EVENT_REFERENCE_PREFIX;
	state->ref.event = &future_event_new()->base;

	zend_object_std_init(&state->std, class_entry);
	object_properties_init(&state->std, class_entry);

	return &state->std;
}

static void future_state_object_free(zend_object *object)
{
	future_state_t *state = future_state_from_object(object);
	async_future_event_t *future = FUTURE_EVENT(state);

	state->ref.event = NULL;
	async_future_event_release(future);

	zend_object_std_dtor(object);
}

static HashTable *future_state_object_gc(zend_object *object, zval **table, int *count)
{
	zend_get_gc_buffer *gc_buffer = zend_get_gc_buffer_create();

	future_event_gc(FUTURE_EVENT(future_state_from_object(object)), gc_buffer);
	zend_get_gc_buffer_use(gc_buffer, table, count);

	return NULL;
}

static zend_object *future_object_create(zend_class_entry *class_entry)
{
	future_t *future_object = zend_object_alloc(sizeof(future_t), class_entry);

	future_object->ref.flags = ASYNC_EVENT_REFERENCE_PREFIX;
	future_object->ref.event = NULL;
	future_object->state = NULL;
	ZVAL_UNDEF(&future_object->mapper);
	future_object->mapper_kind = FUTURE_MAPPER_MAP;

	zend_object_std_init(&future_object->std, class_entry);
	object_properties_init(&future_object->std, class_entry);

	return &future_object->std;
}

/* A Future that holds `future` itself; takes the caller's reference. */
static future_t *future_new(async_future_event_t *future)
{
	future_t *future_object = future_from_object(future_object_create(async_ce_future));
	future_object->ref.event = &future->base;

	return future_object;
}

/* What a Future held: its state object, or its own event when it has no state. */
static void future_release_held(zend_object *state, async_future_event_t *future)
{
	if (state != NULL) {
		OBJ_RELEASE(state);
	} else if (future != NULL) {
		async_future_event_release(future);
	}
}

static void future_release_holding(future_t *future_object)
{
	zend_object *state = future_object->state;
	async_future_event_t *future = FUTURE_EVENT(future_object);

	future_object->state = NULL;
	future_object->ref.event = NULL;
	future_release_held(state, future);
}

static void future_object_free(zend_object *object)
{
	future_t *future_object = future_from_object(object);

	zval_ptr_dtor(&future_object->mapper);
	ZVAL_UNDEF(&future_object->mapper);
	future_release_holding(future_object);

	zend_object_std_dtor(object);
}

static HashTable *future_object_gc(zend_object *object, zval **table, int *count)
{
	future_t *future_object = future_from_object(object);
	zend_get_gc_buffer *gc_buffer = zend_get_gc_buffer_create();

	if (future_object->state != NULL) {
		zend_get_gc_buffer_add_obj(gc_buffer, future_object->state);
	} else {
		future_event_gc(FUTURE_EVENT(future_object), gc_buffer);
	}

	zend_get_gc_buffer_add_zval(gc_buffer, &future_object->mapper);
	zend_get_gc_buffer_use(gc_buffer, table, count);

	return NULL;
}

zend_object *async_future_new_pending(async_future_event_t **event)
{
	async_future_event_t *future = future_event_new();

	*event = future;

	return &future_new(future)->std;
}

void async_future_event_resolve(async_future_event_t *future, zval *result, zend_object *exception)
{
	future->base.ref_count++;
	future_event_complete(future, result, exception, NULL);
	async_future_event_release(future);
}

void async_future_collector_references(zend_object *object, async_collector_t *collector)
{
	if (UNEXPECTED(object->ce == async_ce_future_state)) {
		async_collector_report_event(
				collector, future_state_from_object(object)->ref.event, future_event_collector_references);
		return;
	}

	future_t *future_object = future_from_object(object);

	if (EXPECTED(future_object->state != NULL)) {
		async_collector_report_object(collector, future_object->state);
	} else if (EXPECTED(future_object->ref.event != NULL)) {
		async_collector_report_event(collector, future_object->ref.event, future_event_collector_references);
	}

	async_collector_report_zval(collector, &future_object->mapper);
}

async_future_event_t *async_future_event_from_object(zend_object *object)
{
	ZEND_ASSERT(object->ce == async_ce_future);

	return FUTURE_EVENT(future_from_object(object));
}

///////////////////////////////////////////////////////////////////
/// Shared by both classes
///////////////////////////////////////////////////////////////////

static void future_throw_completed(const async_future_event_t *future)
{
	if (future->completed_filename != NULL) {
		zend_throw_exception_ex(async_ce_async_exception,
								0,
								"FutureState is already completed at %s:%u",
								ZSTR_VAL(future->completed_filename),
								future->completed_lineno);
	} else {
		zend_throw_exception(async_ce_async_exception, "FutureState is already completed at Unknown:0", 0);
	}
}

static void future_awaiting_info(const async_future_event_t *future, zval *return_value)
{
	if (future == NULL) {
		RETURN_EMPTY_ARRAY();
	}

	const char *line = (future->base.flags & ASYNC_EVENT_F_CLOSED) ? "FutureState(completed)" : "FutureState(pending)";

	array_init_size(return_value, 1);
	add_next_index_string(return_value, line);
}

static void future_file_and_line(zend_string *filename, const uint32_t lineno, zval *return_value)
{
	array_init_size(return_value, 2);

	if (filename != NULL) {
		add_next_index_str(return_value, zend_string_copy(filename));
	} else {
		add_next_index_null(return_value);
	}

	add_next_index_long(return_value, lineno);
}

static void future_location(const zend_string *filename, const uint32_t lineno, zval *return_value)
{
	if (filename == NULL) {
		RETURN_STRING("unknown");
	}

	RETURN_STR(zend_strpprintf(0, "%s:%u", ZSTR_VAL(filename), lineno));
}

///////////////////////////////////////////////////////////////////
/// FutureState methods
///////////////////////////////////////////////////////////////////

ZEND_METHOD(Async_FutureState, __construct)
{
	ZEND_PARSE_PARAMETERS_NONE();
}

ZEND_METHOD(Async_FutureState, complete)
{
	zval *result = NULL;

	ZEND_PARSE_PARAMETERS_START(1, 1)
		Z_PARAM_ZVAL(result)
	ZEND_PARSE_PARAMETERS_END();

	async_future_event_t *future = FUTURE_EVENT(THIS_FUTURE_STATE);

	if (UNEXPECTED(future->base.flags & ASYNC_EVENT_F_CLOSED)) {
		future_throw_completed(future);
		RETURN_THROWS();
	}

	future_event_complete(future, result, NULL, NULL);
}

ZEND_METHOD(Async_FutureState, error)
{
	zend_object *throwable = NULL;

	ZEND_PARSE_PARAMETERS_START(1, 1)
		Z_PARAM_OBJ_OF_CLASS(throwable, zend_ce_throwable)
	ZEND_PARSE_PARAMETERS_END();

	async_future_event_t *future = FUTURE_EVENT(THIS_FUTURE_STATE);

	if (UNEXPECTED(future->base.flags & ASYNC_EVENT_F_CLOSED)) {
		future_throw_completed(future);
		RETURN_THROWS();
	}

	future_event_complete(future, NULL, throwable, NULL);
}

ZEND_METHOD(Async_FutureState, isCompleted)
{
	ZEND_PARSE_PARAMETERS_NONE();

	RETURN_BOOL(FUTURE_EVENT(THIS_FUTURE_STATE)->base.flags & ASYNC_EVENT_F_CLOSED);
}

ZEND_METHOD(Async_FutureState, ignore)
{
	ZEND_PARSE_PARAMETERS_NONE();

	FUTURE_EVENT(THIS_FUTURE_STATE)->base.flags |= ASYNC_FUTURE_F_IGNORED;
}

ZEND_METHOD(Async_FutureState, getAwaitingInfo)
{
	ZEND_PARSE_PARAMETERS_NONE();

	future_awaiting_info(FUTURE_EVENT(THIS_FUTURE_STATE), return_value);
}

ZEND_METHOD(Async_FutureState, getCreatedFileAndLine)
{
	ZEND_PARSE_PARAMETERS_NONE();

	const async_future_event_t *future = FUTURE_EVENT(THIS_FUTURE_STATE);
	future_file_and_line(future->created_filename, future->created_lineno, return_value);
}

ZEND_METHOD(Async_FutureState, getCreatedLocation)
{
	ZEND_PARSE_PARAMETERS_NONE();

	const async_future_event_t *future = FUTURE_EVENT(THIS_FUTURE_STATE);
	future_location(future->created_filename, future->created_lineno, return_value);
}

ZEND_METHOD(Async_FutureState, getCompletedFileAndLine)
{
	ZEND_PARSE_PARAMETERS_NONE();

	const async_future_event_t *future = FUTURE_EVENT(THIS_FUTURE_STATE);
	future_file_and_line(future->completed_filename, future->completed_lineno, return_value);
}

ZEND_METHOD(Async_FutureState, getCompletedLocation)
{
	ZEND_PARSE_PARAMETERS_NONE();

	const async_future_event_t *future = FUTURE_EVENT(THIS_FUTURE_STATE);
	future_location(future->completed_filename, future->completed_lineno, return_value);
}

///////////////////////////////////////////////////////////////////
/// Future methods
///////////////////////////////////////////////////////////////////

/* The event of the Future of ZEND_THIS, or NULL with an AsyncException for one never constructed. */
static async_future_event_t *this_future_event(zval *this_zval)
{
	async_future_event_t *future = FUTURE_EVENT(future_from_object(Z_OBJ_P(this_zval)));

	if (UNEXPECTED(future == NULL)) {
		zend_throw_exception(async_ce_async_exception, "Future has no state", 0);
	}

	return future;
}

ZEND_METHOD(Async_Future, completed)
{
	zval *value = NULL;

	ZEND_PARSE_PARAMETERS_START(0, 1)
		Z_PARAM_OPTIONAL
		Z_PARAM_ZVAL(value)
	ZEND_PARSE_PARAMETERS_END();

	zval null_value;
	ZVAL_NULL(&null_value);

	async_future_event_t *future = future_event_new();
	future_event_complete(future, value != NULL ? value : &null_value, NULL, NULL);

	RETURN_OBJ(&future_new(future)->std);
}

ZEND_METHOD(Async_Future, failed)
{
	zend_object *throwable = NULL;

	ZEND_PARSE_PARAMETERS_START(1, 1)
		Z_PARAM_OBJ_OF_CLASS(throwable, zend_ce_throwable)
	ZEND_PARSE_PARAMETERS_END();

	async_future_event_t *future = future_event_new();
	future_event_complete(future, NULL, throwable, NULL);

	RETURN_OBJ(&future_new(future)->std);
}

ZEND_METHOD(Async_Future, __construct)
{
	zend_object *state = NULL;

	ZEND_PARSE_PARAMETERS_START(1, 1)
		Z_PARAM_OBJ_OF_CLASS(state, async_ce_future_state)
	ZEND_PARSE_PARAMETERS_END();

	future_t *future_object = THIS_FUTURE;
	zend_object *previous_state = future_object->state;
	async_future_event_t *previous_future = FUTURE_EVENT(future_object);

	GC_ADDREF(state);
	future_object->state = state;
	future_object->ref.event = future_state_from_object(state)->ref.event;

	/* A second call lets go of what the first one gave it once the new state is in place: the
	 * release may run a destructor that suspends, and a drain coroutine may then take this
	 * Future's item. */
	future_release_held(previous_state, previous_future);
}

ZEND_METHOD(Async_Future, isCompleted)
{
	ZEND_PARSE_PARAMETERS_NONE();

	const async_future_event_t *future = FUTURE_EVENT(THIS_FUTURE);

	RETURN_BOOL(future != NULL && (future->base.flags & ASYNC_EVENT_F_CLOSED));
}

ZEND_METHOD(Async_Future, isCancelled)
{
	ZEND_PARSE_PARAMETERS_NONE();

	const async_future_event_t *future = FUTURE_EVENT(THIS_FUTURE);

	RETURN_BOOL(future != NULL && future->exception != NULL &&
				instanceof_function(future->exception->ce, async_ce_cancellation));
}

/* TrueAsync's Future::cancel (future.c:1258-1288): a completed future stays as it is. */
ZEND_METHOD(Async_Future, cancel)
{
	zend_object *cancellation = NULL;

	ZEND_PARSE_PARAMETERS_START(0, 1)
		Z_PARAM_OPTIONAL
		Z_PARAM_OBJ_OF_CLASS_OR_NULL(cancellation, async_ce_cancellation)
	ZEND_PARSE_PARAMETERS_END();

	async_future_event_t *future = this_future_event(ZEND_THIS);

	if (UNEXPECTED(future == NULL)) {
		RETURN_THROWS();
	}

	if (future->base.flags & ASYNC_EVENT_F_CLOSED) {
		return;
	}

	if (cancellation != NULL) {
		future_event_complete(future, NULL, cancellation, NULL);
		return;
	}

	cancellation = async_new_exception(async_ce_cancellation, "Future has been cancelled");
	future_event_complete(future, NULL, cancellation, NULL);
	OBJ_RELEASE(cancellation);
}

ZEND_METHOD(Async_Future, ignore)
{
	ZEND_PARSE_PARAMETERS_NONE();

	async_future_event_t *future = FUTURE_EVENT(THIS_FUTURE);

	if (EXPECTED(future != NULL)) {
		future->base.flags |= ASYNC_FUTURE_F_IGNORED;
	}

	RETURN_OBJ_COPY(Z_OBJ_P(ZEND_THIS));
}

/* TrueAsync's async_future_create_mapper (future.c:1678-1778): the child joins the source's chain,
 * or, on a completed source, goes to a drain of its own at once. */
static void future_mapper_create(INTERNAL_FUNCTION_PARAMETERS, const future_mapper_kind_t mapper_kind)
{
	zval *callable = NULL;

	ZEND_PARSE_PARAMETERS_START(1, 1)
		Z_PARAM_ZVAL(callable)
	ZEND_PARSE_PARAMETERS_END();

	if (UNEXPECTED(!zend_is_callable(callable, 0, NULL))) {
		zend_argument_type_error(1, "must be of type callable, %s given", zend_zval_type_name(callable));
		RETURN_THROWS();
	}

	async_future_event_t *source = this_future_event(ZEND_THIS);

	if (UNEXPECTED(source == NULL)) {
		RETURN_THROWS();
	}

	future_t *child = future_new(future_event_new());

	ZVAL_COPY(&child->mapper, callable);
	child->mapper_kind = mapper_kind;
	source->base.flags |= ASYNC_EVENT_F_RESULT_USED;

	if (source->base.flags & ASYNC_EVENT_F_CLOSED) {
		future_drain_t *drain = future_drain_new(1);

		GC_ADDREF(&child->std);
		future_drain_push(drain, source, &child->std);
		future_drain_start(drain);

		if (UNEXPECTED(EG(exception) != NULL)) {
			OBJ_RELEASE(&child->std);
			RETURN_THROWS();
		}

		RETURN_OBJ(&child->std);
	}

	future_chain_reserve(&source->chain, 1);
	GC_ADDREF(&child->std);
	source->chain.children[source->chain.length++] = &child->std;

	RETURN_OBJ(&child->std);
}

ZEND_METHOD(Async_Future, map)
{
	future_mapper_create(INTERNAL_FUNCTION_PARAM_PASSTHRU, FUTURE_MAPPER_MAP);
}

ZEND_METHOD(Async_Future, catch)
{
	future_mapper_create(INTERNAL_FUNCTION_PARAM_PASSTHRU, FUTURE_MAPPER_CATCH);
}

ZEND_METHOD(Async_Future, finally)
{
	future_mapper_create(INTERNAL_FUNCTION_PARAM_PASSTHRU, FUTURE_MAPPER_FINALLY);
}

/* TrueAsync's Future::await (future.c:1304-1390): the result marked used on entry, the exception
 * caught when it is thrown in place or delivered by the wake. */
ZEND_METHOD(Async_Future, await)
{
	zend_object *cancellation = NULL;

	ZEND_PARSE_PARAMETERS_START(0, 1)
		Z_PARAM_OPTIONAL
		Z_PARAM_OBJ_OF_CLASS_OR_NULL(cancellation, async_ce_completable)
	ZEND_PARSE_PARAMETERS_END();

	THROW_IF_UNAVAILABLE();

	async_future_event_t *future = this_future_event(ZEND_THIS);

	if (UNEXPECTED(future == NULL)) {
		RETURN_THROWS();
	}

	/* Marked before the token is read, as TrueAsync's (future.c:1329): a refused token leaves no
	 * warning for the future. */
	future->base.flags |= ASYNC_EVENT_F_RESULT_USED;

	if ((future->base.flags & ASYNC_EVENT_F_CLOSED) && future->exception != NULL) {
		future->base.flags |= ASYNC_EVENT_F_EXC_CAUGHT;
	}

	async_awaitable_t *token = NULL;

	if (cancellation != NULL) {
		token = async_await_awaitable_of(cancellation);

		if (UNEXPECTED(token == NULL)) {
			RETURN_THROWS();
		}

		/* The future as its own token completes as it does: no token, as Async\await() drops it. */
		if (token == (const async_awaitable_t *) future) {
			token = NULL;
		}
	}

	if (token == NULL) {
		async_future_await(future, return_value, NULL);
		return;
	}

	/* The wait's own reference, as Async\await() takes it. */
	async_awaitable_addref(token);
	async_future_await(future, return_value, token);
	async_awaitable_release(token);
}

ZEND_METHOD(Async_Future, getAwaitingInfo)
{
	ZEND_PARSE_PARAMETERS_NONE();

	future_awaiting_info(FUTURE_EVENT(THIS_FUTURE), return_value);
}

ZEND_METHOD(Async_Future, getCreatedFileAndLine)
{
	ZEND_PARSE_PARAMETERS_NONE();

	const async_future_event_t *future = FUTURE_EVENT(THIS_FUTURE);

	if (UNEXPECTED(future == NULL)) {
		future_file_and_line(NULL, 0, return_value);
		return;
	}

	future_file_and_line(future->created_filename, future->created_lineno, return_value);
}

ZEND_METHOD(Async_Future, getCreatedLocation)
{
	ZEND_PARSE_PARAMETERS_NONE();

	const async_future_event_t *future = FUTURE_EVENT(THIS_FUTURE);

	if (UNEXPECTED(future == NULL)) {
		RETURN_STRING("unknown");
	}

	future_location(future->created_filename, future->created_lineno, return_value);
}

ZEND_METHOD(Async_Future, getCompletedFileAndLine)
{
	ZEND_PARSE_PARAMETERS_NONE();

	const async_future_event_t *future = FUTURE_EVENT(THIS_FUTURE);

	if (UNEXPECTED(future == NULL)) {
		future_file_and_line(NULL, 0, return_value);
		return;
	}

	future_file_and_line(future->completed_filename, future->completed_lineno, return_value);
}

ZEND_METHOD(Async_Future, getCompletedLocation)
{
	ZEND_PARSE_PARAMETERS_NONE();

	const async_future_event_t *future = FUTURE_EVENT(THIS_FUTURE);

	if (UNEXPECTED(future == NULL)) {
		RETURN_STRING("unknown");
	}

	future_location(future->completed_filename, future->completed_lineno, return_value);
}

///////////////////////////////////////////////////////////////////
/// Registration
///////////////////////////////////////////////////////////////////

void async_register_future_ce(zend_class_entry *completable_interface)
{
	async_ce_future_state = register_class_Async_FutureState();
	async_ce_future_state->create_object = future_state_object_create;
	async_ce_future_state->default_object_handlers = &future_state_handlers;

	memcpy(&future_state_handlers, zend_get_std_object_handlers(), sizeof(zend_object_handlers));
	future_state_handlers.offset = offsetof(future_state_t, std);
	future_state_handlers.free_obj = future_state_object_free;
	future_state_handlers.get_gc = future_state_object_gc;
	/* A clone would be a second writing end of nothing, or a Future with no event. */
	future_state_handlers.clone_obj = NULL;

	async_ce_future = register_class_Async_Future(completable_interface);
	async_ce_future->create_object = future_object_create;
	async_ce_future->default_object_handlers = &future_handlers;

	memcpy(&future_handlers, zend_get_std_object_handlers(), sizeof(zend_object_handlers));
	future_handlers.offset = offsetof(future_t, std);
	future_handlers.free_obj = future_object_free;
	future_handlers.get_gc = future_object_gc;
	future_handlers.clone_obj = NULL;
}
