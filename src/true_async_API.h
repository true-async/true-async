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
 * not have. A wait is a set of records on the waiting frame's C stack, each linked into one
 * target's callbacks vector; the waker of the waiting coroutine points at the first record. Every
 * structure here is sized for the hot path (dev/plans/S3.md, section 3, which also gives the
 * offsets).
 *
 * Codes in the extension's comments: Dn is item n of dev/reviews/s3-structures/EDMOND-DECISIONS.md,
 * Un an unlink site (dev/plans/S3.md, section 4.4), Bn a benchmark (dev/plans/S3.md, section 12). */

#include "php.h"
#include "Zend/zend_async_API.h"
#include "Zend/zend_exceptions.h"

typedef struct _async_coroutine_s async_coroutine_t;

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
#define ASYNC_COROUTINE_F_HI_PRIORITY (1u << 21)

///////////////////////////////////////////////////////////////////
/// Callbacks and wait records
///////////////////////////////////////////////////////////////////

typedef struct _async_event_callback_s async_event_callback_t;
typedef struct _async_wait_kind_s async_wait_kind_t;

/* Called by a notify of `target`; `result` and `exception` are borrowed for the call. */
typedef void (*async_event_callback_fn)(async_awaitable_t *target,
										async_event_callback_t *callback,
										void *result,
										zend_object *exception);
/* Frees a heap subscriber that leaves a vector by teardown or removal. */
typedef void (*async_event_callback_dispose_fn)(async_event_callback_t *callback, async_awaitable_t *target);

/* A wait record: lives on the waiting frame's stack, never disposed. */
#define ASYNC_CALLBACK_F_RECORD (1u << 0)
/* A record that counts as an external wait (a timer, IO): S4 onward. */
#define ASYNC_CALLBACK_F_COUNTED (1u << 1)
/* kind->unlink is set: removal goes through it instead of the target's vector. */
#define ASYNC_CALLBACK_F_TYPED (1u << 2)

struct _async_event_callback_s
{
	uint32_t flags; /* 4 B of padding follow */
	async_event_callback_fn callback;

	union
	{
		async_event_callback_dispose_fn dispose; /* heap subscribers */
		const async_wait_kind_t *kind;           /* ASYNC_CALLBACK_F_RECORD */
	};
};

/* One wait-graph edge: the waiter, the target, and the kind in event_callback.kind. */
typedef struct
{
	async_event_callback_t event_callback;
	async_coroutine_t *coroutine;
	/* Non-NULL exactly while the record is in that target's callbacks; owns no reference. Whoever
	 * removes the record clears it. */
	async_awaitable_t *event;
} async_coroutine_event_callback_t;

/* One const descriptor per wait kind; the code that links a record chooses it, so a target needs
 * no class and no methods table. */
struct _async_wait_kind_s
{
	zend_coroutine_awaiting_info_fn info;                     /* data = the record */
	void (*unlink)(async_coroutine_event_callback_t *record); /* NULL: removal from the vector */
	void (*abort)(async_coroutine_event_callback_t *record);  /* the frame never runs again; NULL: nothing */
	void (*completers)(async_coroutine_event_callback_t *record, void *walker); /* S7 */
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
	async_callbacks_slots(vector)[vector->length++] = callback;
}

static zend_always_inline void async_callbacks_add(async_callbacks_vector_t *vector, async_event_callback_t *callback)
{
	async_callbacks_reserve(vector, 1);
	async_callbacks_push_reserved(vector, callback);
}

/* Removes `callback`; false when it is not in the vector. Order is not kept. During a notify of
 * the vector, every callback the notify has not reached yet still runs at most once, and the
 * removed one does not run again. Allocates nothing, runs no PHP code. */
bool async_callbacks_remove(async_callbacks_vector_t *vector, async_event_callback_t *callback);

/* Runs the vector's callbacks, each once (order not kept: a removal moves elements), including
 * those added meanwhile, in scheduler context (ZEND_ASYNC_IN_SCHEDULER_CONTEXT). The first callback
 * that throws ends the notify, as in TrueAsync: the rest stay in the vector uncalled, and the thrown
 * exception is chained over the one pending at entry and left in EG(exception). A bailout out of
 * a callback leaves the vector marked, so later notifies of it run nothing (as in TrueAsync). A vector
 * already being notified further up the stack is not notified again. The caller holds a reference
 * to `target` for the call (S3.5's finalize does), so no callback frees the vector. */
void async_callbacks_notify(async_awaitable_t *target,
							async_callbacks_vector_t *vector,
							void *result,
							zend_object *exception);

/* Teardown of the vector of `target` with its owner: disposes the heap subscribers left in it and
 * frees the array. Never called during a notify of the vector, but the vector may still be marked by
 * a bailout out of one. A wait record still linked here breaks invariant F (section 4): asserted,
 * and its target cleared. */
void async_callbacks_free(async_awaitable_t *target, async_callbacks_vector_t *vector);

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

/* Adds an RFC finish handler to the coroutine's vector; returns its id, stable across the removal
 * of other handlers (a position would shift). The handler fires at most once, on the coroutine's
 * notify (not when a callback before it throws), and is dropped before it runs; its return value
 * has no meaning (the core's own handlers return false). */
uint32_t async_finish_handler_add(async_coroutine_t *coroutine,
								  zend_coroutine_finish_handler_fn handler,
								  zend_coroutine_t *waiter,
								  void *data);

/* Removes the finish handler `handler_id`; false when there is none. */
bool async_finish_handler_remove(async_coroutine_t *coroutine, uint32_t handler_id);

///////////////////////////////////////////////////////////////////
/// The waker
///////////////////////////////////////////////////////////////////

/* Per-coroutine wait state (40 B). Owns `error` and `result` only; the records it points to live
 * on the waiting frame's stack. */
typedef struct
{
	zend_object *error;                     /* delivered at the next switch-in */
	zval result;                            /* moved out by the waiter; cleared on the error exit */
	async_coroutine_event_callback_t *wait; /* first record of the current wait; NULL: none linked */
	uint32_t wait_count;                    /* the records of one wait are contiguous */
} async_waker_t;

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
