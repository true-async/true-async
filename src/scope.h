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
#ifndef TRUE_ASYNC_SCOPE_H
#define TRUE_ASYNC_SCOPE_H

#include "php.h"
#include "true_async_API.h"
#include "coroutine.h"
#include "collector.h"
#include "reactor.h"

/* Scopes (dev/plans/S9-scope.md): a tree of groups of coroutines, cancelled together. The scope and
 * its PHP object Async\Scope are two allocations, as TrueAsync's: the scope outlives its object
 * until its last coroutine finishes, and the object's destruction starts its disposal. The global
 * scope, which spawn() uses at the top level, and the root of the engine's own coroutines have no
 * object and live for the request. */

/* A scope's child scopes; each knows its index (`child_index`), so it leaves in O(1). */
typedef struct
{
	async_scope_t **data;
	uint32_t length;
	uint32_t capacity;
} async_scopes_vector_t;

/* A scope's coroutines; each knows its index (`scope_index`), so it leaves in O(1). */
typedef struct
{
	async_coroutine_t **data;
	uint32_t length;
	uint32_t capacity;
} async_coroutines_vector_t;

/* The scope's bits of its event flags word, TrueAsync's (F: zend_async_API.h:1560-1569). */
#define ASYNC_SCOPE_F_CLOSED (1u << ASYNC_EVENT_F_TYPE_SHIFT) /* spawn refused; a cancel does nothing */
/* A cancel makes a started coroutine a zombie instead of interrupting it. */
#define ASYNC_SCOPE_F_DISPOSE_SAFELY (1u << (ASYNC_EVENT_F_TYPE_SHIFT + 1))
#define ASYNC_SCOPE_F_CANCELLED (1u << (ASYNC_EVENT_F_TYPE_SHIFT + 2))
/* The global scope and the engine's: never disposed by being empty, freed at the request's end. */
#define ASYNC_SCOPE_F_REQUEST_LIFETIME (1u << (ASYNC_EVENT_F_TYPE_SHIFT + 3))
/* A disposal's walk is starting the finally handlers of this scope or below it: a refused start disposes
 * the run's scope, and the disposal that passes up must not free this one under the walk (TrueAsync's
 * DISPOSING). */
#define ASYNC_SCOPE_F_DISPOSING (1u << (ASYNC_EVENT_F_TYPE_SHIFT + 4))
/* A finally run's scope while its handlers run: the cancels of other scopes skip it, and from outside only
 * an ancestor's deadline stops it (S9-scope.md 13). Set and cleared by the run alone; no PHP API sets or
 * reads it. */
#define ASYNC_SCOPE_F_FINALLY_RUN (1u << (ASYNC_EVENT_F_TYPE_SHIFT + 5))
/* The scope's dispose timer has fired and interrupted its members, or the scope is a finally run a deadline
 * stopped: no finally run at or below this scope calls another handler (S9-scope.md 14). Never cleared; no
 * PHP API sets or reads it. */
#define ASYNC_SCOPE_F_DEADLINE_PASSED (1u << (ASYNC_EVENT_F_TYPE_SHIFT + 6))
/* Scope::allowZombies() was called: a TaskGroup given the scope warns that its cancel interrupts anyway
 * (S9-taskgroup.md 5). DISPOSE_SAFELY cannot tell, since every scope inherits it from the global scope. */
#define ASYNC_SCOPE_F_ZOMBIES_ALLOWED (1u << (ASYNC_EVENT_F_TYPE_SHIFT + 7))

struct _async_scope_s
{
	async_event_t event;
	zend_object *scope_object; /* NULL without an object, or once it is destroyed */
	async_scope_t *parent_scope;
	async_scopes_vector_t child_scopes;
	async_coroutines_vector_t coroutines;
	uint32_t active_coroutines_count; /* coroutines that are not zombies */
	uint32_t zombie_coroutines_count;
	/* setExceptionHandler() and setChildScopeExceptionHandler(); not initialized while unset. */
	zend_fcall_info_cache exception_handler;
	zend_fcall_info_cache child_exception_handler;
	zend_string *filename; /* where the scope was made; NULL outside PHP code */
	uint32_t lineno;
	uint32_t child_index; /* in the parent's `child_scopes` */
	/* The await_* walk whose exception cancels this scope (await.c); NULL for any other scope, and once
	 * the walk has finished. */
	async_coroutine_t *iterator_coroutine;
	/* disposeAfterTimeout()'s Timer op while armed, NULL otherwise; the scope owns one reference to it.
	 * The scope's free withdraws it, and its close unless a finally run below still needs it. */
	async_io_event_t *dispose_timer;
	async_event_callback_t dispose_timer_callback; /* in `dispose_timer`'s vector while armed */
	HashTable *finally_handlers;                   /* lazy: the closures of Scope::finally() */
	zend_object *context;                          /* lazy: the scope's Async\Context, one reference */
	/* The TaskGroup whose own scope this is, borrowed: while set, the scope is not disposed, as one with
	 * coroutines is not (S9-taskgroup.md 5). Cleared by async_scope_release_owner(), async_scope_forget_owner()
	 * and the request's end, which frees the scope. */
	zend_object *owner_object;
};

/* Async\Scope. A stand-in is the object a SpawnStrategy's hooks get for a scope without one; it stays
 * the scope's object while anything holds it, and its destruction never cancels the scope. */
typedef struct
{
	async_scope_t *scope; /* NULL once the scope is gone */
	bool is_cancelled;    /* the scope's CANCELLED, kept for isCancelled() after it is gone */
	bool is_stand_in;
	zend_object std;
} async_scope_object_t;

extern zend_class_entry *async_ce_scope;
extern zend_class_entry *async_ce_scope_provider;
extern zend_class_entry *async_ce_spawn_strategy;

void async_register_scope_ce(void);

/* Creates the global scope and the engine's scope of the request. */
void async_scope_request_startup(void);

/* Takes every coroutine left in the registry out of its scope, disposing the scopes that leaves with
 * nothing, then frees the two request scopes with their child scopes; their objects are detached and
 * stay with whoever holds them. Called before the registry's coroutines are released. A root scope of
 * user code that still has its object is freed by the object. The handlers, contexts and scope objects
 * it drops go to `released_values`, made on the first one, for the caller to release. */
void async_scope_request_shutdown(zend_array **released_values);

/* The scope spawn() uses: the current coroutine's, or the global scope when no coroutine runs or the
 * current one has no scope (a Fiber's, the scheduler's, a finished one's, as TrueAsync). */
async_scope_t *async_scope_current(void);

/* The scope's Async\Context, made at the first call, a closed or cancelled scope's too; borrowed. */
zend_object *async_scope_context(async_scope_t *scope);

/* A scope with no object below `parent_scope`, whose safe disposal it takes, or a root when NULL; one
 * without an object goes with its last coroutine unless a TaskGroup pins it (`owner_object`). */
async_scope_t *async_scope_new(async_scope_t *parent_scope);

/* Adds `coroutine`, which belongs to no scope, to `scope`. */
void async_scope_add_coroutine(async_scope_t *scope, async_coroutine_t *coroutine);

/* Takes `coroutine` out of its scope, which it leaves finished or never queued, and disposes the scope
 * once nothing keeps it: a disposal releases the handlers and the context, whose destructors may run PHP
 * code, or starts the finally handlers. Otherwise the scope objects that only their objects reach now go
 * back to the GC's root buffer, up to the first live one: a collection may have found them live. */
void async_scope_remove_coroutine(async_coroutine_t *coroutine);

/* Takes `coroutine`, which the scheduler refused to enqueue, out of its scope and the registry and
 * releases it: it leaves the request as if it had never existed. */
void async_scope_discard_coroutine(async_coroutine_t *coroutine);

/* The cancel slot's `is_safely` for a started coroutine: it becomes a zombie, which its scope and
 * get_coroutine_count() no longer count as active, and runs on. A second call does nothing. */
void async_scope_mark_zombie(async_coroutine_t *coroutine);

/* Scope::cancel(): the child scopes and the coroutines are cancelled with `error`, or with
 * AsyncCancellation("Scope was cancelled") when it is NULL; with `is_safely` a started coroutine becomes a
 * zombie instead. A closed scope and a finally run's scope ignore it; a scope with nothing left to cancel is
 * closed and its finally handlers start (TrueAsync's catch_or_cancel in CANCEL mode, scope.c:942-1080), and,
 * unlike TrueAsync's, the cancel goes on to each child scope with no coroutine of its own. A transferred
 * `error` is the callee's. */
void async_scope_cancel(async_scope_t *scope, zend_object *error, bool transfer_error, bool is_safely);

/* Cancels, never safely, every unfinished coroutine of `scope` and of its child scopes, those an earlier
 * cancel reached too: async_scope_cancel() counts a cancelled scope as completed and cancels nothing more in it,
 * so a coroutine spawned into it after its cancel, a zombie, or one that caught its cancellation and waits again
 * would run on. A finally run's scope is left alone. For a TaskGroup's closing (S9-taskgroup.md 5, step 6).
 * `error` stays the caller's. */
void async_scope_cancel_remaining(async_scope_t *scope, zend_object *error);

/* Clears the owner pin of `scope` and disposes the scope when nothing else keeps it: a disposal may run PHP
 * code, as async_scope_remove_coroutine()'s. */
void async_scope_release_owner(async_scope_t *scope);

/* Clears the owner pin of `scope` and disposes nothing: for a group that may run no PHP code (after a bailout,
 * in its free). */
void async_scope_forget_owner(async_scope_t *scope);

/* Called by the iterator as a finally run's last worker leaves: the run's scope takes scope cancels again.
 * Any other iterator's scope is left alone. */
void async_scope_finally_run_end(async_scope_t *scope);

/* True once `scope` or an ancestor carries ASYNC_SCOPE_F_DEADLINE_PASSED. */
bool async_scope_is_past_deadline(const async_scope_t *scope);

/* The route of an unhandled error of `coroutine`, which belongs to a scope (S9-scope.md 4, TrueAsync's
 * catch_or_cancel in CATCH mode, scope.c:942-1080): from the coroutine's scope up to its root, each
 * scope's handler is called, and a scope whose handler does not take the error is cancelled with its
 * child scopes and coroutines, a started one becoming a zombie when the coroutine's own scope disposes
 * safely. True when a handler took the error or called exit(), which ends the request, or when a waiter
 * on a scope of the route was woken with it. Runs PHP code; `error` stays the caller's. */
bool async_scope_catch(async_coroutine_t *coroutine, zend_object *error);

/* For the collector (S7.md 10): a candidate `coroutine` at `node` is reached from its scope's reach node,
 * each scope's from its parent's, and a scope's node is live while its object, or the await_* walk
 * that cancels it, is live. A cancel goes down the tree: an object reaches its scope's coroutines and
 * those of its child scopes. */
void async_scope_collector_reach(async_collector_t *collector, async_coroutine_t *coroutine, uint32_t node);

/* For a channel bound to `scope` (S9-channel.md 6): the reach node `node` is live once something can
 * cancel the scope, as async_scope_collector_reach() reports it from there, or, for a scope that its last
 * member's end frees (no object, or cancelled), once a coroutine of its subtree can run or a child
 * scope's object can be released. */
void async_scope_collector_bound_channel_reach(async_collector_t *collector, async_scope_t *scope, uint32_t node);

/* Starts the callable in a new coroutine of `scope`, the body of spawn(), Scope::spawn() and
 * spawn_with(): the cache comes from Z_PARAM_FUNC_NO_TRAMPOLINE_FREE and goes with the coroutine, or
 * is released on refusal. A `spawn_strategy` has its hooks called around the enqueue. Returns the
 * coroutine with a reference the caller owns; NULL with an exception when the scope is closed, the
 * scheduler cannot take the coroutine or a hook throws. */
async_coroutine_t *async_scope_spawn(async_scope_t *scope,
									 zend_object *spawn_strategy,
									 zend_fcall_info *fci,
									 zend_fcall_info_cache *fcc,
									 zval *args,
									 uint32_t args_count,
									 HashTable *named_args);

static zend_always_inline async_scope_object_t *async_scope_object_from_object(zend_object *object)
{
	return (async_scope_object_t *) ((char *) object - offsetof(async_scope_object_t, std));
}

#endif /* TRUE_ASYNC_SCOPE_H */
