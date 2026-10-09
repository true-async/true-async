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
#ifndef TRUE_ASYNC_API_H
#define TRUE_ASYNC_API_H

/* The extension's internal async API: what TrueAsync kept in zend_async_API and the RFC core does
 * not have. A wait is a record linked into the target's callbacks vector; the record lives in the
 * waiting coroutine's waker. Every structure here is sized for the hot path
 * (dev/plans/S3.md, section 3, which also gives the offsets).
 *
 * Codes in the extension's comments: Dn is item n of dev/reviews/s3-structures/EDMOND-DECISIONS.md,
 * Un an unlink site (dev/plans/S3.md, section 4.4), Bn a benchmark and On an optimisation option
 * (dev/plans/S3.md, section 12). */

#include "php.h"
#include "Zend/zend_async_API.h"
#include "Zend/zend_exceptions.h"

typedef struct _async_coroutine_s async_coroutine_t;
typedef struct _async_scope_s async_scope_t; /* scope.h */

///////////////////////////////////////////////////////////////////
/// Awaitables: the type bit
///////////////////////////////////////////////////////////////////

/* Every awaitable starts with a 32-bit flags word. Bit 31 tells a coroutine (0) from an event (1);
 * zeroed memory is a coroutine, and every event constructor sets the bit. Never instantiated:
 * generic wait code takes this type and branches on the bit. */
typedef struct _async_awaitable_s
{
	uint32_t flags;
} async_awaitable_t;

#define ASYNC_AWAITABLE_F_EVENT (1u << 31)
#define ASYNC_AWAITABLE_IS_COROUTINE(awaitable) \
	((((const async_awaitable_t *) (awaitable))->flags & ASYNC_AWAITABLE_F_EVENT) == 0)

/* Extension bits of the coroutine flags word; bits 0-15 belong to the core (section 2). */
#define ASYNC_COROUTINE_F_PROTECTED (1u << 16)
#define ASYNC_COROUTINE_F_EXCEPTION_HANDLED (1u << 17)
#define ASYNC_COROUTINE_F_EXC_CAUGHT (1u << 18)
#define ASYNC_COROUTINE_F_BAILOUT (1u << 19)
/* A cancel with is_safely found it started: it runs on, outside its scope's active count and the
 * core's coroutine count (scope.h). Never cleared. */
#define ASYNC_COROUTINE_F_ZOMBIE (1u << 20)
#define ASYNC_COROUTINE_F_HI_PRIORITY (1u << 21)
/* The collector warned that the coroutine can never wake (collector.h); never cleared. */
#define ASYNC_COROUTINE_F_DEADLOCK_REPORTED (1u << 22)
/* The collector's `cancel` policy cancelled it (collector.h); never cleared. */
#define ASYNC_COROUTINE_F_DEADLOCK_CANCELLED (1u << 25)
/* The coroutine left a scope other than the global one: a finished one has no scope to read a context
 * from (current_context()). Never cleared. */
#define ASYNC_COROUTINE_F_LEFT_NON_GLOBAL_SCOPE (1u << 26)
/* The body's call has returned or thrown: a cancel of the running coroutine leaves it as it is, so a
 * destructor that the release of its callable runs cannot replace its outcome (S9-taskgroup.md 2). Never
 * cleared. */
#define ASYNC_COROUTINE_F_BODY_RETURNED (1u << 27)
#ifdef TRUE_ASYNC_TEST_HOOKS
/* The collector's oracle (collector.h): found by a run, the one in `found_run` last; never cleared. */
#define ASYNC_COROUTINE_F_DEADLOCK_FOUND (1u << 23)
/* The oracle's excuse: handed out to PHP code or cancelled by the collector's policy, or woken by
 * something excused (collector.h). */
#define ASYNC_COROUTINE_F_HANDED_OUT (1u << 24)
#endif

///////////////////////////////////////////////////////////////////
/// Callbacks and wait records
///////////////////////////////////////////////////////////////////

typedef struct _async_event_callback_s async_event_callback_t;

/* Called by a notify of `target`; `result` and `exception` are borrowed for the call. */
typedef void (*async_event_callback_fn)(async_awaitable_t *target,
										async_event_callback_t *callback,
										void *result,
										zend_object *exception);
/* Frees a heap subscriber that leaves a vector by teardown or removal. */
typedef void (*async_event_callback_dispose_fn)(async_event_callback_t *callback, async_awaitable_t *target);

/* A wait record: part of the waiting coroutine's wait, never disposed. */
#define ASYNC_CALLBACK_F_RECORD (1u << 0)
/* A record whose kind has an unlink: the generic unlink calls it instead of removing the record from
 * the target's vector itself. */
#define ASYNC_CALLBACK_F_TYPED (1u << 1)
/* One object pushed into vectors of several threads: it keeps no slot and is found by a search. */
#define ASYNC_CALLBACK_F_SHARED (1u << 2)
/* A record its target keeps outside its vector (a channel's queue entry): the unlinks of this layer
 * leave it linked, and its waiting frame removes it after its suspend returns; when that frame never
 * runs again, its kind's abort removes it and clears `event` (dev/plans/S9-channel.md, section 3). */
#define ASYNC_CALLBACK_F_FRAME_UNLINKS (1u << 3)
/* Bits 8-31 of a record's flags belong to its kind. */
#define ASYNC_CALLBACK_F_KIND_SHIFT 8

typedef struct _async_wait_kind_s async_wait_kind_t;
typedef struct _async_collector_s async_collector_t;

struct _async_event_callback_s
{
	uint32_t flags;
	/* The index in the vector the callback is in, so removing it searches nothing under fan-in. */
	uint32_t slot;
	async_event_callback_fn callback;

	union
	{
		async_event_callback_dispose_fn dispose; /* a heap subscriber; may be NULL */
		const async_wait_kind_t *kind;           /* a record (F_RECORD) */
	};
};

/* One wait-graph edge: the waiter and the target. */
typedef struct
{
	async_event_callback_t event_callback;
	async_coroutine_t *coroutine;
	/* Non-NULL exactly while the record is in that target's callbacks; owns no reference. Whoever
	 * removes the record clears it. */
	async_awaitable_t *event;
} async_coroutine_event_callback_t;

/* What a wait does with its target beyond its vector, one const table per kind (dev/plans/S4.md 2.1):
 * the code that links a record knows its target's type and picks the kind, so events carry no
 * methods (D28). */
struct _async_wait_kind_s
{
	/* One line of getAwaitingInfo() and of the deadlock report for a linked record. */
	zend_string *(*info)(const async_coroutine_event_callback_t *record);
	/* Removes a linked record from its target and clears its `event`, for a target that holds more
	 * than the vector (an op to orphan, a list to leave). NULL: the generic removal. */
	void (*unlink)(async_coroutine_event_callback_t *record);
	/* Runs before the unlink when the waiting frame never runs again (a bailout's transfer, the
	 * request's end) and removes the typed state that frame would remove after its wake (S9's
	 * channel queue entry, D29). NULL: nothing. */
	void (*abort)(async_coroutine_event_callback_t *record);
	/* Reports the record's target to the collector of coroutines that can never wake (collector.h):
	 * whoever reaches the target can end the wait. NULL: an outside source may end it, and the waiter
	 * is never reported. */
	void (*collector_target)(const async_coroutine_event_callback_t *record, async_collector_t *collector);
};

///////////////////////////////////////////////////////////////////
/// The callbacks vector
///////////////////////////////////////////////////////////////////

/* Subscribers of one awaitable. Up to one element lives inline (capacity 0), so the first waiter
 * costs no allocation. Removal keeps every pending callback of a running notify: see
 * async_callbacks_remove(). */
typedef struct
{
	union
	{
		async_event_callback_t **data;           /* capacity > 0 */
		async_event_callback_t *inline_callback; /* capacity == 0: at most one element */
	};

	uint32_t length;
	uint32_t capacity; /* bit 31: a notify is iterating this vector */
	uint32_t cursor;   /* that notify's next callback; meaningless without bit 31 */
} async_callbacks_vector_t;

#define ASYNC_CALLBACKS_F_NOTIFYING (1u << 31)
#define ASYNC_CALLBACKS_CAPACITY(vector) ((vector)->capacity & ~ASYNC_CALLBACKS_F_NOTIFYING)

/* The element array, inline or on the heap. */
static zend_always_inline async_event_callback_t **async_callbacks_slots(async_callbacks_vector_t *vector)
{
	return ASYNC_CALLBACKS_CAPACITY(vector) == 0 ? &vector->inline_callback : vector->data;
}

/* A subscriber's callback for a notify it has nothing to do in: it acts in its dispose. */
void async_callback_ignore(async_awaitable_t *target,
						   async_event_callback_t *callback,
						   void *result,
						   zend_object *exception);

/* Makes room for `count` more elements; may allocate, and so bail out on OOM. A push into
 * reserved room never allocates: a wait reserves before its first link (section 4, invariant L). */
void async_callbacks_reserve(async_callbacks_vector_t *vector, uint32_t count);

/* Appends into room made by async_callbacks_reserve(). Allowed during a notify of the vector: the
 * new element runs in that notify. */
static zend_always_inline void async_callbacks_push_reserved(async_callbacks_vector_t *vector,
															 async_event_callback_t *callback)
{
	const uint32_t capacity = ASYNC_CALLBACKS_CAPACITY(vector);

	ZEND_ASSERT(vector->length < (capacity == 0 ? 1 : capacity));

	if (EXPECTED(!(callback->flags & ASYNC_CALLBACK_F_SHARED))) {
		callback->slot = vector->length;
	}

	async_callbacks_slots(vector)[vector->length++] = callback;
}

/* Removes `callback`; false when it is not in the vector. Order is not kept. A callback is found
 * by its slot; a shared one, or one not in the vector, by a search. During a notify of
 * the vector, every callback the notify has not reached yet still runs at most once, and the
 * removed one does not run again. Allocates nothing, runs no PHP code. */
bool async_callbacks_remove(async_callbacks_vector_t *vector, async_event_callback_t *callback);

/* Runs the vector's callbacks, each once (order not kept: a removal moves elements), including
 * those added meanwhile, in scheduler context (ZEND_ASYNC_IN_SCHEDULER_CONTEXT). The first callback
 * that throws ends the notify, as in TrueAsync: the rest stay in the vector uncalled, and the thrown
 * exception is chained over the one pending at entry and left in EG(exception). A bailout out of
 * a callback leaves the vector marked, so later notifies of it run nothing (as in TrueAsync). A vector
 * already being notified further up the stack is not notified again. The caller holds a reference
 * to `target` for the call (async_coroutine_finalize does), so no callback frees the vector. True
 * when it called a wait record: a coroutine was parked on `target`. */
bool async_callbacks_notify(async_awaitable_t *target,
							async_callbacks_vector_t *vector,
							void *result,
							zend_object *exception);

/* Teardown of the vector of `target` with its owner: disposes the heap subscribers left in it and
 * frees the array. Never called during a notify of the vector, but the vector may still be marked by
 * a bailout out of one. A wait record found here, left by a callback that threw and ended the
 * notify, is detached and its callback runs with no result, which wakes the waiter (the target is
 * finished, and the waiter reads the outcome from it). */
void async_callbacks_free(async_awaitable_t *target, async_callbacks_vector_t *vector);

///////////////////////////////////////////////////////////////////
/// Events
///////////////////////////////////////////////////////////////////

/* Event flags at the positions of TrueAsync's fork (dev/plans/S3.md 3.7); bits 13-30 are an event
 * type's own. */
/* Bit 30 marks a Timeout's event. */
#define ASYNC_EVENT_F_CLOSED (1u << 0)            /* a one-shot event fired; a new waiter reads its outcome */
#define ASYNC_EVENT_F_RESULT_USED (1u << 1)       /* somebody took the outcome */
#define ASYNC_EVENT_F_EXC_CAUGHT (1u << 2)        /* somebody took the exception */
#define ASYNC_EVENT_F_ZEND_OBJ (1u << 4)          /* inside its object: `object_offset`, not `ref_count` */
#define ASYNC_EVENT_F_EXCEPTION_HANDLED (1u << 6) /* a waiter woken with the exception */
#define ASYNC_EVENT_F_REFERENCE (1u << 7)         /* only in an object's reference prefix */
#define ASYNC_EVENT_F_TYPE_SHIFT 13

/* An awaitable that is not a coroutine (32 B). Its type's code allocates, references and frees it;
 * the last release tears the vector down with async_callbacks_free(), which wakes a waiter whose
 * record is still linked. */
typedef struct _async_event_s
{
	uint32_t flags; /* ASYNC_AWAITABLE_F_EVENT and the ASYNC_EVENT_F_ bits */

	union
	{
		uint32_t ref_count;     /* without ZEND_OBJ: the holders, records excluded */
		uint32_t object_offset; /* with ZEND_OBJ: from the event to its zend_object */
	};

	async_callbacks_vector_t callbacks;
} async_event_t;

/* The prefix of an object that points at an event it does not contain (a Future and its state share
 * one, S5): the object's handlers->offset leads here. */
typedef struct
{
	uint32_t flags; /* ASYNC_EVENT_REFERENCE_PREFIX */
	async_event_t *event;
} async_event_ref_t;

#define ASYNC_EVENT_REFERENCE_PREFIX (ASYNC_AWAITABLE_F_EVENT | ASYNC_EVENT_F_REFERENCE)

/* Starts an event with one reference and no subscribers; `type_flags` are its type's own bits. */
static zend_always_inline void async_event_init(async_event_t *event, const uint32_t type_flags)
{
	event->flags = ASYNC_AWAITABLE_F_EVENT | type_flags;
	event->ref_count = 1;
	memset(&event->callbacks, 0, sizeof(event->callbacks));
}

/* Starts an event that lives inside its zend_object, `object_offset` bytes before it: the object's
 * references count for it. */
static zend_always_inline void
async_event_init_in_object(async_event_t *event, const uint32_t type_flags, const uint32_t object_offset)
{
	event->flags = ASYNC_AWAITABLE_F_EVENT | ASYNC_EVENT_F_ZEND_OBJ | type_flags;
	event->object_offset = object_offset;
	memset(&event->callbacks, 0, sizeof(event->callbacks));
}

/* The awaitable of an object of this extension: a coroutine, an event inside the object, or the
 * event its reference prefix points at. The object's handlers->offset leads to one of the three. */
static zend_always_inline async_awaitable_t *async_awaitable_from_object(zend_object *object)
{
	ZEND_ASSERT(object->handlers->offset != 0 && "an object without an awaitable before it");

	async_awaitable_t *base = (async_awaitable_t *) ((char *) object - object->handlers->offset);

	if (UNEXPECTED((base->flags & ASYNC_EVENT_REFERENCE_PREFIX) == ASYNC_EVENT_REFERENCE_PREFIX)) {
		return (async_awaitable_t *) ((async_event_ref_t *) base)->event;
	}

	return base;
}

///////////////////////////////////////////////////////////////////
/// Finish handlers
///////////////////////////////////////////////////////////////////

/* An RFC finish handler stored as a callback in the coroutine's vector (56 B). */
typedef struct
{
	async_event_callback_t event_callback;
	zend_coroutine_finish_handler_fn handler;
	zend_coroutine_t *waiter;
	void *data;
	uint32_t handler_id; /* never 0 */
} async_finish_handler_callback_t;

/* The add_finish_handler slot: adds an RFC finish handler to the coroutine's vector; returns its id, stable across the
 * removal of other handlers (a position would shift). The handler fires at most once, on the coroutine's notify (not
 * when a callback before it throws), and is dropped before it runs; its return value has no meaning (the core's own
 * handlers return false). One that replaces the coroutine's exception owns the old one's reference (to release or
 * chain) and gives the coroutine a new outcome; the callbacks after it in the same notify get the old one as their
 * argument. */
uint32_t async_finish_handler_add(zend_coroutine_t *coroutine,
								  zend_coroutine_finish_handler_fn handler,
								  zend_coroutine_t *waiter,
								  void *data);

/* The remove_finish_handler slot: removes the finish handler `handler_id`; false when there is none. */
bool async_finish_handler_remove(zend_coroutine_t *coroutine, uint32_t handler_id);

///////////////////////////////////////////////////////////////////
/// Switch handlers
///////////////////////////////////////////////////////////////////

typedef struct
{
	zend_coroutine_switch_handler_fn handler;
	uint32_t handler_id; /* never 0 */
} async_switch_handler_t;

/* A coroutine's switch handlers, allocated at the first add and freed when the last one goes, as
 * TrueAsync's (zend_async_API.c:1985-2097 of the fork). The core adds one to the coroutine that
 * runs the shutdown destructors (zend_execute_API.c, zend_objects_API.c). */
struct _async_coroutine_switch_handlers_vector_s
{
	async_switch_handler_t *data;
	uint32_t length;
	uint32_t capacity;
	bool in_execution; /* adds and removes refuse while the handlers run */
};

/* The add_switch_handler slot: adds `handler` once; a second add of the same function returns its id. 0 with a warning
 * while the coroutine's handlers run. */
uint32_t async_switch_handler_add(zend_coroutine_t *coroutine, zend_coroutine_switch_handler_fn handler);

/* The remove_switch_handler slot: removes the switch handler `handler_id`; false when there is none. */
bool async_switch_handler_remove(zend_coroutine_t *coroutine, uint32_t handler_id);

/* Calls every switch handler of a coroutine that has some: is_enter false when it gives up the CPU,
 * true when it runs again. A handler that returns false is dropped. */
void async_switch_handlers_call(async_coroutine_t *coroutine, bool is_enter);

/* Frees the handlers of a coroutine that finishes. */
void async_switch_handlers_free(async_coroutine_t *coroutine);

///////////////////////////////////////////////////////////////////
/// The waker
///////////////////////////////////////////////////////////////////

typedef struct _async_wait_block_s async_wait_block_t;

/* How the layer reaches the records of a block (dev/plans/S4.md 2.2). */
typedef struct
{
	/* Unlinks every record the block holds and ends linking into it (S5 sets its `finished` here);
	 * returns at once once it has run. Allocates nothing, runs no PHP code. */
	void (*unlink)(async_wait_block_t *block);
	/* Drops the waiter's reference; may run PHP code (a reference to an item the block held). Reads
	 * nothing from the waker: another holder (S5's iterator coroutine) may keep the block after the
	 * waiter has moved on. */
	void (*release)(async_wait_block_t *block);
	/* Calls `visit` for each linked record of the block (getAwaitingInfo(), S7's walk of the wait
	 * graph). */
	void (*walk)(async_wait_block_t *block,
				 void (*visit)(const async_coroutine_event_callback_t *record, void *arg),
				 void *arg);
} async_wait_block_ops_t;

/* The head of the records of a wait past the waker's two. The stage that waits embeds it in a block
 * of its own, which holds the records (and S5's await_* context), and links them with
 * async_wait_link(); the layer only calls `ops`. A wait may use the waker's records and a block
 * together (S5's cancellation record beside its items). */
struct _async_wait_block_s
{
	const async_wait_block_ops_t *ops;
};

#define ASYNC_WAKER_INLINE_RECORDS 2

/* Per-coroutine wait state (112 B). Owns `error`, `result` and the waiter's reference to `block`. */
typedef struct
{
	zend_object *error; /* delivered at the next switch-in */
	zval result;        /* moved out by the waiter; cleared on the error exit */
	/* The records of a wait for one or two targets, each linked while its `event` is set, as
	 * TrueAsync's inline callbacks of the waker (two, ZEND_ASYNC_WAKER_INLINE_SLOTS). They live in the
	 * coroutine, not on the waiting frame's stack: a bailout that unwinds the frame leaves them
	 * intact, and the coroutine's finish unlinks them. */
	async_coroutine_event_callback_t records[ASYNC_WAKER_INLINE_RECORDS];
	/* The records past two; NULL without. Kept after the unlink: the waiter takes it with
	 * async_wait_take_block() when its suspend() returns and reads its outcome there. */
	async_wait_block_t *block;
} async_waker_t;

/* Links `record` of `waiter`'s wait into `target`'s vector, into a slot that
 * async_callbacks_reserve() made (no allocation, nothing to undo). `record` is one of the waker's
 * records or one of its block's, and another coroutine may link into a parked waiter's block. No PHP
 * code runs in the waiter between its first link and its suspend(): a wait started there would end
 * this one. `wake` runs on the target's notify. One that enqueues the waiter leaves the unlink to
 * the enqueue, which unlinks the whole wait (D26); one that only records (S5's gathering record short
 * of its count) may unlink its own record. A wake run by the target's teardown finds its record
 * unlinked already (`event` NULL). */
void async_wait_link(async_coroutine_event_callback_t *record,
					 async_coroutine_t *waiter,
					 async_awaitable_t *target,
					 const async_wait_kind_t *kind,
					 async_event_callback_fn wake);

/* Links `record` of `waiter`'s wait to `target` without its vector, as ASYNC_CALLBACK_F_FRAME_UNLINKS:
 * the target's own code holds it and wakes the waiter. Allocates nothing; the rules of async_wait_link()
 * apply. */
void async_wait_link_outside(async_coroutine_event_callback_t *record,
							 async_coroutine_t *waiter,
							 async_awaitable_t *target,
							 const async_wait_kind_t *kind);

/* Removes a linked record from its target's vector and clears its `event`: the generic unlink, and
 * the part a kind's unlink shares with it. */
void async_wait_record_remove(async_coroutine_event_callback_t *record);

/* Unlinks one record when it is linked: its kind's unlink, or the removal from the target's vector.
 * A block's ops->unlink calls it for its records, the target's teardown for a record left there.
 * A record its frame unlinks (ASYNC_CALLBACK_F_FRAME_UNLINKS) is left linked. */
void async_wait_record_unlink(async_coroutine_event_callback_t *record);

/* async_wait_unlink() (coroutine.h) past its check that nothing is linked: unlinks the waker's
 * records and the block's; keeps the block. Allocates nothing, runs no PHP code. */
void async_wait_unlink_linked(async_coroutine_t *coroutine);

/* The unlink for a frame that never runs again (U4, finalize, U6): each linked record's kind abort
 * first. A block's records have no abort: no kind S5 links into a block needs one. */
void async_wait_abort(async_coroutine_t *coroutine);

/* Calls `visit` for each linked record of the coroutine's wait, the block's included. */
void async_wait_walk(async_coroutine_t *coroutine,
					 void (*visit)(const async_coroutine_event_callback_t *record, void *arg),
					 void *arg);

/* Unlinks the coroutine's wait and hands its block to the caller, who owns the waiter's reference
 * from then on; NULL without a block. The waiter calls it as its suspend() returns, so a wait it
 * starts later (a destructor run while it reads the outcome) finds no block to end. Between the
 * wake and this call no PHP code may run in the waiter either: suspend()'s error exit drops
 * `waker.result`, which nothing sets; whoever sets it first moves that drop past this call. */
async_wait_block_t *async_wait_take_block(async_coroutine_t *coroutine);

/* Ends a wait its frame never ended: the unlink, then the waiter's reference to a block left in the
 * waker. A new wait and the coroutine's finish call it after a bailout cut a wait short.
 * A record its frame unlinks is aborted first: that frame never runs again. */
void async_wait_end(async_coroutine_t *coroutine);

///////////////////////////////////////////////////////////////////
/// Exceptions across a park
///////////////////////////////////////////////////////////////////

/* Moves a pending exception aside before a park, chaining an older saved one under it: the fiber
 * switch does not carry EG(exception), and the next coroutine's call would return at once with
 * it set. */
static zend_always_inline void async_exception_save_fast(zend_object **exception, zend_object **saved)
{
	if (UNEXPECTED(*saved)) {
		zend_exception_set_previous(*exception, *saved);
	}

	if (UNEXPECTED(*exception)) {
		*saved = *exception;
	}

	*exception = NULL;
}

/* Puts the saved exception back after a park, under any exception the park raised. */
static zend_always_inline void async_exception_restore_fast(zend_object **exception, zend_object **saved)
{
	if (UNEXPECTED(*saved)) {
		if (*exception) {
			zend_exception_set_previous(*exception, *saved);
		} else {
			*exception = *saved;
		}

		*saved = NULL;
	}
}

#endif /* TRUE_ASYNC_API_H */
