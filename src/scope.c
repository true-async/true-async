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
#include "Zend/zend_exceptions.h"
#include "Zend/zend_interfaces.h"
#include "php_true_async.h"
#include "scope.h"
#include "exceptions.h"
#include "scheduler.h"
#include "scope_arginfo.h"

zend_class_entry *async_ce_scope = NULL;
zend_class_entry *async_ce_scope_provider = NULL;
zend_class_entry *async_ce_spawn_strategy = NULL;

static zend_object_handlers scope_handlers;

///////////////////////////////////////////////////////////////////
/// Membership
///////////////////////////////////////////////////////////////////

static void scope_add_child(async_scope_t *parent_scope, async_scope_t *child_scope)
{
	async_scopes_vector_t *vector = &parent_scope->child_scopes;

	if (vector->length == vector->capacity) {
		vector->capacity = vector->capacity == 0 ? 4 : vector->capacity * 2;
		vector->data = safe_erealloc(vector->data, vector->capacity, sizeof(async_scope_t *), 0);
	}

	vector->data[vector->length++] = child_scope;
	child_scope->parent_scope = parent_scope;
}

static void scope_remove_child(async_scope_t *parent_scope, const async_scope_t *child_scope)
{
	async_scopes_vector_t *vector = &parent_scope->child_scopes;

	for (uint32_t i = 0; i < vector->length; i++) {
		if (vector->data[i] == child_scope) {
			vector->data[i] = vector->data[--vector->length];
			return;
		}
	}
}

void async_scope_add_coroutine(async_scope_t *scope, async_coroutine_t *coroutine)
{
	async_coroutines_vector_t *vector = &scope->coroutines;

	ZEND_ASSERT(coroutine->scope == NULL);

	if (vector->length == vector->capacity) {
		vector->capacity = vector->capacity == 0 ? 4 : vector->capacity * 2;
		vector->data = safe_erealloc(vector->data, vector->capacity, sizeof(async_coroutine_t *), 0);
	}

	coroutine->scope = scope;
	coroutine->scope_index = vector->length;
	vector->data[vector->length++] = coroutine;

	if (UNEXPECTED(coroutine->coroutine.flags & ASYNC_COROUTINE_F_ZOMBIE)) {
		scope->zombie_coroutines_count++;
	} else {
		scope->active_coroutines_count++;
	}
}

/* The last member moves into the gap (section 9, item 1). */
static void scope_detach_coroutine(async_coroutine_t *coroutine)
{
	async_scope_t *scope = coroutine->scope;
	async_coroutines_vector_t *vector = &scope->coroutines;
	async_coroutine_t *last = vector->data[--vector->length];

	ZEND_ASSERT(vector->data[coroutine->scope_index] == coroutine);

	vector->data[coroutine->scope_index] = last;
	last->scope_index = coroutine->scope_index;
	coroutine->scope = NULL;

	if (UNEXPECTED(coroutine->coroutine.flags & ASYNC_COROUTINE_F_ZOMBIE)) {
		scope->zombie_coroutines_count--;
	} else {
		scope->active_coroutines_count--;
	}
}

void async_scope_mark_zombie(async_coroutine_t *coroutine)
{
	if (UNEXPECTED(coroutine->coroutine.flags & ASYNC_COROUTINE_F_ZOMBIE)) {
		return;
	}

	coroutine->coroutine.flags |= ASYNC_COROUTINE_F_ZOMBIE;
	ASYNC_G(zombie_coroutines_count)++;

	async_scope_t *scope = coroutine->scope;

	if (EXPECTED(scope != NULL)) {
		scope->active_coroutines_count--;
		scope->zombie_coroutines_count++;
	}
}

async_scope_t *async_scope_current(void)
{
	const async_coroutine_t *coroutine = (async_coroutine_t *) ZEND_ASYNC_CURRENT_COROUTINE;

	return coroutine != NULL && coroutine->scope != NULL ? coroutine->scope : ASYNC_G(global_scope);
}

///////////////////////////////////////////////////////////////////
/// States
///////////////////////////////////////////////////////////////////

/* No coroutine of the scope or of its children runs, zombies counted or not; a closed or cancelled
 * scope counts as completed whatever runs in it (TrueAsync's can_be_disposed without the object check,
 * scope.c:1503-1555): isFinished(), and cancel()'s test for a scope with nothing left to cancel. */
static bool scope_is_completed(const async_scope_t *scope, const bool with_zombies)
{
	if (scope->event.flags & (ASYNC_SCOPE_F_CLOSED | ASYNC_SCOPE_F_CANCELLED)) {
		return true;
	}

	const uint32_t running = scope->active_coroutines_count + (with_zombies ? scope->zombie_coroutines_count : 0);

	if (EXPECTED(running > 0)) {
		return false;
	}

	for (uint32_t i = 0; i < scope->child_scopes.length; i++) {
		if (!scope_is_completed(scope->child_scopes.data[i], with_zombies)) {
			return false;
		}
	}

	return true;
}

/* Nothing can use the scope any more: no coroutine, zombies included, it is cancelled or its object is
 * gone, and every child scope is the same (TrueAsync's can_be_disposed with both checks). */
static bool scope_can_be_disposed(const async_scope_t *scope)
{
	if (EXPECTED(scope->active_coroutines_count + scope->zombie_coroutines_count > 0 ||
				 (scope->event.flags & ASYNC_SCOPE_F_REQUEST_LIFETIME))) {
		return false;
	}

	if (EXPECTED(!(scope->event.flags & ASYNC_SCOPE_F_CANCELLED) && scope->scope_object != NULL)) {
		return false;
	}

	for (uint32_t i = 0; i < scope->child_scopes.length; i++) {
		if (!scope_can_be_disposed(scope->child_scopes.data[i])) {
			return false;
		}
	}

	return true;
}

static void scope_set_cancelled(async_scope_t *scope)
{
	scope->event.flags |= ASYNC_SCOPE_F_CANCELLED;

	if (scope->scope_object != NULL) {
		async_scope_object_from_object(scope->scope_object)->is_cancelled = true;
	}
}

///////////////////////////////////////////////////////////////////
/// Disposal
///////////////////////////////////////////////////////////////////

/* Frees the scope and its child scopes, whose coroutines are all gone or detached. The object stays
 * with whoever holds it and reads as closed. */
static void scope_free(async_scope_t *scope)
{
	for (uint32_t i = 0; i < scope->child_scopes.length; i++) {
		scope_free(scope->child_scopes.data[i]);
	}

	for (uint32_t i = 0; i < scope->coroutines.length; i++) {
		scope->coroutines.data[i]->scope = NULL;
	}

	if (scope->scope_object != NULL) {
		async_scope_object_from_object(scope->scope_object)->scope = NULL;
	}

	async_callbacks_free((async_awaitable_t *) &scope->event, &scope->event.callbacks);

	if (scope->child_scopes.data != NULL) {
		efree(scope->child_scopes.data);
	}

	if (scope->coroutines.data != NULL) {
		efree(scope->coroutines.data);
	}

	efree(scope);
}

/* TrueAsync's scope_dispose (scope.c:1179-1300) for a scope that can be disposed: it leaves its
 * parent, which the last child to go disposes in turn when it can, and goes with its child scopes. */
static void scope_dispose(async_scope_t *scope)
{
	async_scope_t *parent_scope = scope->parent_scope;

	if (EXPECTED(parent_scope != NULL)) {
		scope_remove_child(parent_scope, scope);
	}

	scope_free(scope);

	if (parent_scope != NULL && parent_scope->child_scopes.length == 0 && scope_can_be_disposed(parent_scope)) {
		scope_dispose(parent_scope);
	}
}

void async_scope_remove_coroutine(async_coroutine_t *coroutine)
{
	async_scope_t *scope = coroutine->scope;

	scope_detach_coroutine(coroutine);

	if (UNEXPECTED(scope_can_be_disposed(scope))) {
		scope_dispose(scope);
	}
}

///////////////////////////////////////////////////////////////////
/// Cancellation
///////////////////////////////////////////////////////////////////

void async_scope_cancel(async_scope_t *scope, zend_object *error, bool transfer_error, const bool is_safely)
{
	if (scope->event.flags & ASYNC_SCOPE_F_CLOSED) {
		if (error != NULL && transfer_error) {
			OBJ_RELEASE(error);
		}

		return;
	}

	if (scope_is_completed(scope, true)) {
		scope->event.flags |= ASYNC_SCOPE_F_CLOSED;

		if (error != NULL && transfer_error) {
			OBJ_RELEASE(error);
		}

		return;
	}

	if (error == NULL) {
		error = async_new_exception(async_ce_cancellation, "Scope was cancelled");
		transfer_error = true;
	}

	scope_set_cancelled(scope);

	/* No PHP code runs on the way: a cancel only queues, so neither vector changes under the loops. */
	for (uint32_t i = 0; i < scope->child_scopes.length; i++) {
		async_scope_cancel(scope->child_scopes.data[i], error, false, is_safely);
	}

	for (uint32_t i = 0; i < scope->coroutines.length; i++) {
		async_coroutine_cancel(scope->coroutines.data[i], error, false, is_safely);
	}

	if (transfer_error) {
		OBJ_RELEASE(error);
	}
}

///////////////////////////////////////////////////////////////////
/// Spawn
///////////////////////////////////////////////////////////////////

/* A coroutine that never got into the run queue leaves the request as if it had never existed. */
static void spawn_discard(async_coroutine_t *coroutine)
{
	async_scope_remove_coroutine(coroutine);
	zend_hash_index_del(&ASYNC_G(coroutines), coroutine->std.handle);
	OBJ_RELEASE(&coroutine->std);
}

/* A scope without an object (the global scope) gets a stand-in for the hooks of a SpawnStrategy, its
 * object while anything holds it (S9-scope.md 9, item 8). */
static zend_object *spawn_stand_in_attach(async_scope_t *scope)
{
	async_scope_object_t *stand_in = zend_object_alloc(sizeof(async_scope_object_t), async_ce_scope);

	zend_object_std_init(&stand_in->std, async_ce_scope);
	object_properties_init(&stand_in->std, async_ce_scope);
	stand_in->scope = scope;
	stand_in->is_cancelled = (scope->event.flags & ASYNC_SCOPE_F_CANCELLED) != 0;
	stand_in->is_stand_in = true;
	scope->scope_object = &stand_in->std;

	return &stand_in->std;
}

static bool spawn_strategy_call(zend_object *spawn_strategy,
								const char *method_name,
								const size_t method_name_length,
								async_coroutine_t *coroutine,
								zend_object *scope_object)
{
	zval coroutine_value, scope_value, retval;

	ZVAL_OBJ(&coroutine_value, &coroutine->std);
	ZVAL_OBJ(&scope_value, scope_object);
	zend_call_method(spawn_strategy,
					 spawn_strategy->ce,
					 NULL,
					 method_name,
					 method_name_length,
					 &retval,
					 2,
					 &coroutine_value,
					 &scope_value);
	zval_ptr_dtor(&retval);

	return EG(exception) == NULL;
}

/* A hook threw: the coroutine ends cancelled, so whoever kept it sees it end, and the hook's exception
 * stays the pending one. */
static void spawn_cancel(async_coroutine_t *coroutine)
{
	zend_object *hook_exception = EG(exception);

	EG(exception) = NULL;

	/* The cancel's enqueue fails only without a stack for the scheduler; the coroutine then never ran. */
	if (UNEXPECTED(!async_coroutine_cancel(coroutine, NULL, false, false))) {
		zend_clear_exception();

		if (ZEND_COROUTINE_STATUS(&coroutine->coroutine) == ZEND_COROUTINE_STATUS_CREATED) {
			spawn_discard(coroutine);
		}
	}

	EG(exception) = hook_exception;
}

/* The hooks may suspend: the coroutine may finish meanwhile and take its scope with it, so this frame
 * holds the coroutine and does not touch the scope after the first hook. */
static async_coroutine_t *
spawn_with_strategy(async_scope_t *scope, zend_object *spawn_strategy, async_coroutine_t *coroutine)
{
	zend_object *scope_object = scope->scope_object;

	if (EXPECTED(scope_object != NULL)) {
		GC_ADDREF(scope_object);
	} else {
		scope_object = spawn_stand_in_attach(scope);
	}

	GC_ADDREF(&coroutine->std);

	bool succeeded = spawn_strategy_call(spawn_strategy, ZEND_STRL("beforeCoroutineEnqueue"), coroutine, scope_object);

	if (EXPECTED(succeeded && !ZEND_COROUTINE_IS_FINISHED(&coroutine->coroutine))) {
		/* Refused only when the scheduler coroutine cannot get a stack, so the coroutine is still CREATED,
		 * or QUEUED by a hook, which the enqueue accepts. */
		if (UNEXPECTED(!async_scheduler_enqueue(&coroutine->coroutine, NULL, false))) {
			spawn_discard(coroutine);
			OBJ_RELEASE(&coroutine->std);
			coroutine = NULL;
		} else {
			succeeded =
					spawn_strategy_call(spawn_strategy, ZEND_STRL("afterCoroutineEnqueue"), coroutine, scope_object);
		}
	}

	OBJ_RELEASE(scope_object);

	if (UNEXPECTED(coroutine == NULL)) {
		return NULL;
	}

	if (UNEXPECTED(!succeeded)) {
		spawn_cancel(coroutine);
		OBJ_RELEASE(&coroutine->std);
		return NULL;
	}

	return coroutine;
}

async_coroutine_t *async_scope_spawn(async_scope_t *scope,
									 zend_object *spawn_strategy,
									 zend_fcall_info *fci,
									 zend_fcall_info_cache *fcc,
									 zval *args,
									 const uint32_t args_count,
									 HashTable *named_args)
{
	if (UNEXPECTED(scope->event.flags & ASYNC_SCOPE_F_CLOSED)) {
		zend_release_fcall_info_cache(fcc);
		zend_throw_exception(async_ce_async_exception, "Cannot spawn a coroutine in a closed scope", 0);
		return NULL;
	}

	ASYNC_IO_PROVIDER_INSTALL_ONCE();

	async_coroutine_t *coroutine = async_coroutine_new();

	/* ZEND_ASYNC_FCALL_DEFINE into the coroutine's own block. */
	zend_fcall_t *fcall = &coroutine->spawn_fcall;
	fcall->fci = *fci;
	fcall->fci_cache = *fcc;

	if (args_count != 0) {
		fcall->fci.param_count = args_count;
		fcall->fci.params = safe_emalloc(args_count, sizeof(zval), 0);

		for (uint32_t i = 0; i < args_count; i++) {
			ZVAL_COPY(&fcall->fci.params[i], &args[i]);
		}
	}

	if (UNEXPECTED(named_args != NULL)) {
		fcall->fci.named_params = named_args;
		GC_ADDREF(named_args);
	}

	Z_TRY_ADDREF(fcall->fci.function_name);

	/* The call comes after this frame, and the callable's name alone may not resolve again: the cache
	 * keeps the object a class-string callable resolved to ($this of the spawning method) and a __call
	 * trampoline, which the call consumes. */
	zend_fcc_addref(&fcall->fci_cache);
	coroutine->coroutine.fcall = fcall;

	zend_string *filename = zend_get_executed_filename_ex();

	coroutine->coroutine.filename = filename != NULL ? zend_string_copy(filename) : NULL;
	coroutine->coroutine.lineno = zend_get_executed_lineno();

	async_scope_add_coroutine(scope, coroutine);

	if (UNEXPECTED(spawn_strategy != NULL)) {
		return spawn_with_strategy(scope, spawn_strategy, coroutine);
	}

	/* A CREATED coroutine is refused only when the scheduler coroutine cannot get a stack: the coroutine
	 * then never existed. */
	if (UNEXPECTED(!async_scheduler_enqueue(&coroutine->coroutine, NULL, false))) {
		spawn_discard(coroutine);
		return NULL;
	}

	GC_ADDREF(&coroutine->std);

	return coroutine;
}

///////////////////////////////////////////////////////////////////
/// The object
///////////////////////////////////////////////////////////////////

/* A scope with no coroutine yet, below `parent_scope` (whose safe disposal it takes), or a root. */
static async_scope_t *scope_new(async_scope_t *parent_scope)
{
	async_scope_t *scope = ecalloc(1, sizeof(async_scope_t));

	async_event_init(&scope->event, 0);

	if (parent_scope != NULL) {
		scope->event.flags |= parent_scope->event.flags & ASYNC_SCOPE_F_DISPOSE_SAFELY;
		scope_add_child(parent_scope, scope);
	}

	return scope;
}

static zend_object *scope_object_new(zend_class_entry *class_entry, async_scope_t *parent_scope)
{
	async_scope_object_t *scope_object = zend_object_alloc(sizeof(async_scope_object_t), class_entry);

	zend_object_std_init(&scope_object->std, class_entry);
	object_properties_init(&scope_object->std, class_entry);

	scope_object->scope = scope_new(parent_scope);
	scope_object->scope->scope_object = &scope_object->std;
	scope_object->is_cancelled = false;
	scope_object->is_stand_in = false;

	return &scope_object->std;
}

/* `new Scope()`: a root with unsafe disposal (TrueAsync's async_new_scope, scope.c:1302-1381). */
static zend_object *scope_object_create(zend_class_entry *class_entry)
{
	return scope_object_new(class_entry, NULL);
}

/* The object goes, the scope stays until its coroutines finish: disposed now when nothing keeps it,
 * else cancelled when `may_cancel` (TrueAsync's scope_destroy, scope.c:1395-1416). */
static void scope_object_release_scope(async_scope_object_t *scope_object, const bool may_cancel)
{
	async_scope_t *scope = scope_object->scope;

	scope_object->scope = NULL;
	scope->scope_object = NULL;

	if (scope_can_be_disposed(scope)) {
		scope_dispose(scope);
		return;
	}

	if (may_cancel) {
		zend_object *error =
				async_new_exception(async_ce_cancellation, "Scope is being disposed due to object destruction");

		async_scope_cancel(scope, error, true, (scope->event.flags & ASYNC_SCOPE_F_DISPOSE_SAFELY) != 0);
	}
}

static void scope_object_destroy(zend_object *object)
{
	async_scope_object_t *scope_object = async_scope_object_from_object(object);

	if (scope_object->scope != NULL) {
		scope_object_release_scope(scope_object, !scope_object->is_stand_in);
	}
}

/* The destructor did not run: the constructor failed, or a bailout skipped the shutdown's destructors.
 * Once the request's scheduler is gone (the core turned async off) nothing is cancelled: a scope the
 * object cannot dispose stays to the end of the request's memory. */
static void scope_object_free(zend_object *object)
{
	async_scope_object_t *scope_object = async_scope_object_from_object(object);

	if (scope_object->scope != NULL) {
		scope_object_release_scope(scope_object, ZEND_ASYNC_IS_ACTIVE && !scope_object->is_stand_in);
	}

	zend_object_std_dtor(object);
}

///////////////////////////////////////////////////////////////////
/// Methods
///////////////////////////////////////////////////////////////////

#define THIS_SCOPE_OBJECT (async_scope_object_from_object(Z_OBJ_P(ZEND_THIS)))

/* The scope of $this, or NULL with an exception once it is gone. */
static async_scope_t *this_scope(zval *this_value)
{
	async_scope_t *scope = async_scope_object_from_object(Z_OBJ_P(this_value))->scope;

	if (UNEXPECTED(scope == NULL)) {
		zend_throw_exception(async_ce_async_exception, "Scope object has been disposed", 0);
	}

	return scope;
}

ZEND_METHOD(Async_Scope, inherit)
{
	zend_object *parent_object = NULL;

	ZEND_PARSE_PARAMETERS_START(0, 1)
		Z_PARAM_OPTIONAL
		Z_PARAM_OBJ_OF_CLASS_OR_NULL(parent_object, async_ce_scope)
	ZEND_PARSE_PARAMETERS_END();

	async_scope_t *parent_scope = async_scope_current();

	if (parent_object != NULL) {
		parent_scope = async_scope_object_from_object(parent_object)->scope;

		if (UNEXPECTED(parent_scope == NULL)) {
			zend_throw_exception(async_ce_async_exception,
								 "Cannot inherit a Scope from a parent Scope that has already been disposed.",
								 0);
			RETURN_THROWS();
		}
	}

	RETURN_OBJ(scope_object_new(async_ce_scope, parent_scope));
}

ZEND_METHOD(Async_Scope, provideScope)
{
	ZEND_PARSE_PARAMETERS_NONE();

	RETURN_OBJ_COPY(Z_OBJ_P(ZEND_THIS));
}

ZEND_METHOD(Async_Scope, __construct)
{
	ZEND_PARSE_PARAMETERS_NONE();
}

ZEND_METHOD(Async_Scope, asNotSafely)
{
	ZEND_PARSE_PARAMETERS_NONE();

	async_scope_t *scope = this_scope(ZEND_THIS);

	if (UNEXPECTED(scope == NULL)) {
		RETURN_THROWS();
	}

	scope->event.flags &= ~ASYNC_SCOPE_F_DISPOSE_SAFELY;

	RETURN_OBJ_COPY(Z_OBJ_P(ZEND_THIS));
}

ZEND_METHOD(Async_Scope, allowZombies)
{
	ZEND_PARSE_PARAMETERS_NONE();

	async_scope_t *scope = this_scope(ZEND_THIS);

	if (UNEXPECTED(scope == NULL)) {
		RETURN_THROWS();
	}

	scope->event.flags |= ASYNC_SCOPE_F_DISPOSE_SAFELY;

	RETURN_OBJ_COPY(Z_OBJ_P(ZEND_THIS));
}

ZEND_METHOD(Async_Scope, spawn)
{
	zend_fcall_info fci;
	zend_fcall_info_cache fcc;
	zval *args = NULL;
	uint32_t args_count = 0;
	HashTable *named_args = NULL;

	THROW_IF_UNAVAILABLE();

	ZEND_PARSE_PARAMETERS_START(1, -1)
		Z_PARAM_FUNC_NO_TRAMPOLINE_FREE(fci, fcc)
		Z_PARAM_VARIADIC_WITH_NAMED(args, args_count, named_args)
	ZEND_PARSE_PARAMETERS_END();

	async_scope_t *scope = this_scope(ZEND_THIS);

	if (UNEXPECTED(scope == NULL)) {
		zend_release_fcall_info_cache(&fcc);
		RETURN_THROWS();
	}

	async_coroutine_t *coroutine = async_scope_spawn(scope, NULL, &fci, &fcc, args, args_count, named_args);

	if (UNEXPECTED(coroutine == NULL)) {
		RETURN_THROWS();
	}

	RETURN_OBJ(&coroutine->std);
}

ZEND_METHOD(Async_Scope, cancel)
{
	zend_object *cancellation = NULL;

	ZEND_PARSE_PARAMETERS_START(0, 1)
		Z_PARAM_OPTIONAL
		Z_PARAM_OBJ_OF_CLASS_OR_NULL(cancellation, async_ce_cancellation)
	ZEND_PARSE_PARAMETERS_END();

	async_scope_t *scope = THIS_SCOPE_OBJECT->scope;

	if (scope != NULL) {
		async_scope_cancel(scope, cancellation, false, (scope->event.flags & ASYNC_SCOPE_F_DISPOSE_SAFELY) != 0);
	}
}

ZEND_METHOD(Async_Scope, isFinished)
{
	ZEND_PARSE_PARAMETERS_NONE();

	const async_scope_t *scope = THIS_SCOPE_OBJECT->scope;

	RETURN_BOOL(scope == NULL || scope_is_completed(scope, false));
}

ZEND_METHOD(Async_Scope, isClosed)
{
	ZEND_PARSE_PARAMETERS_NONE();

	const async_scope_t *scope = THIS_SCOPE_OBJECT->scope;

	RETURN_BOOL(scope == NULL || (scope->event.flags & ASYNC_SCOPE_F_CLOSED));
}

ZEND_METHOD(Async_Scope, isCancelled)
{
	ZEND_PARSE_PARAMETERS_NONE();

	const async_scope_object_t *scope_object = THIS_SCOPE_OBJECT;

	if (scope_object->scope == NULL) {
		RETURN_BOOL(scope_object->is_cancelled);
	}

	RETURN_BOOL(scope_object->scope->event.flags & ASYNC_SCOPE_F_CANCELLED);
}

ZEND_METHOD(Async_Scope, getChildScopes)
{
	ZEND_PARSE_PARAMETERS_NONE();

	const async_scope_t *scope = THIS_SCOPE_OBJECT->scope;

	if (scope == NULL) {
		RETURN_EMPTY_ARRAY();
	}

	array_init_size(return_value, scope->child_scopes.length);

	for (uint32_t i = 0; i < scope->child_scopes.length; i++) {
		zend_object *child_object = scope->child_scopes.data[i]->scope_object;

		if (child_object != NULL) {
			GC_ADDREF(child_object);
			add_next_index_object(return_value, child_object);
		}
	}
}

///////////////////////////////////////////////////////////////////
/// spawn_with()
///////////////////////////////////////////////////////////////////

/* What `provider` names, with `scope_value` holding the object it returned until the spawn is done: a
 * Scope nobody else holds would be disposed under it. The current scope for null; NULL with an
 * exception for anything else (TrueAsync's async_provide_scope, async_API.c:32-58) or a Scope that is
 * gone. */
static async_scope_t *scope_provide(zend_object *provider, zval *scope_value)
{
	zend_call_method(provider, provider->ce, NULL, ZEND_STRL("provideScope"), scope_value, 0, NULL, NULL);

	if (UNEXPECTED(EG(exception) != NULL)) {
		return NULL;
	}

	if (Z_TYPE_P(scope_value) == IS_NULL) {
		return async_scope_current();
	}

	if (UNEXPECTED(Z_TYPE_P(scope_value) != IS_OBJECT ||
				   !instanceof_function(Z_OBJCE_P(scope_value), async_ce_scope))) {
		zend_throw_exception(async_ce_async_exception, "Scope provider must return an instance of Async\\Scope", 0);
		return NULL;
	}

	async_scope_t *scope = async_scope_object_from_object(Z_OBJ_P(scope_value))->scope;

	if (UNEXPECTED(scope == NULL)) {
		zend_throw_exception(async_ce_async_exception, "Scope object has been disposed", 0);
	}

	return scope;
}

ZEND_FUNCTION(Async_spawn_with)
{
	zend_object *provider = NULL;
	zend_fcall_info fci;
	zend_fcall_info_cache fcc;
	zval *args = NULL;
	uint32_t args_count = 0;
	HashTable *named_args = NULL;

	THROW_IF_UNAVAILABLE();

	ZEND_PARSE_PARAMETERS_START(2, -1)
		Z_PARAM_OBJ_OF_CLASS(provider, async_ce_scope_provider)
		Z_PARAM_FUNC_NO_TRAMPOLINE_FREE(fci, fcc)
		Z_PARAM_VARIADIC_WITH_NAMED(args, args_count, named_args)
	ZEND_PARSE_PARAMETERS_END();

	zval scope_value;
	async_scope_t *scope = scope_provide(provider, &scope_value);
	async_coroutine_t *coroutine = NULL;

	if (EXPECTED(scope != NULL)) {
		zend_object *spawn_strategy = instanceof_function(provider->ce, async_ce_spawn_strategy) ? provider : NULL;
		coroutine = async_scope_spawn(scope, spawn_strategy, &fci, &fcc, args, args_count, named_args);
	} else {
		zend_release_fcall_info_cache(&fcc);
	}

	zval_ptr_dtor(&scope_value);

	if (UNEXPECTED(coroutine == NULL)) {
		RETURN_THROWS();
	}

	RETURN_OBJ(&coroutine->std);
}

///////////////////////////////////////////////////////////////////
/// The request
///////////////////////////////////////////////////////////////////

void async_scope_request_startup(void)
{
	ASYNC_G(global_scope) = scope_new(NULL);
	ASYNC_G(global_scope)->event.flags |= ASYNC_SCOPE_F_DISPOSE_SAFELY | ASYNC_SCOPE_F_REQUEST_LIFETIME;
	ASYNC_G(engine_scope) = scope_new(NULL);
	ASYNC_G(engine_scope)->event.flags |= ASYNC_SCOPE_F_REQUEST_LIFETIME;
	ASYNC_G(zombie_coroutines_count) = 0;
}

void async_scope_request_shutdown(void)
{
	async_coroutine_t *coroutine = NULL;

	ZEND_HASH_FOREACH_PTR(&ASYNC_G(coroutines), coroutine)
	{
		if (coroutine->scope != NULL) {
			async_scope_remove_coroutine(coroutine);
		}
	}
	ZEND_HASH_FOREACH_END();

	scope_free(ASYNC_G(global_scope));
	scope_free(ASYNC_G(engine_scope));
	ASYNC_G(global_scope) = NULL;
	ASYNC_G(engine_scope) = NULL;
}

void async_register_scope_ce(void)
{
	async_ce_scope_provider = register_class_Async_ScopeProvider();
	async_ce_spawn_strategy = register_class_Async_SpawnStrategy(async_ce_scope_provider);
	async_ce_scope = register_class_Async_Scope(async_ce_scope_provider);
	async_ce_scope->create_object = scope_object_create;
	async_ce_scope->default_object_handlers = &scope_handlers;

	memcpy(&scope_handlers, zend_get_std_object_handlers(), sizeof(zend_object_handlers));
	scope_handlers.offset = offsetof(async_scope_object_t, std);
	scope_handlers.dtor_obj = scope_object_destroy;
	scope_handlers.free_obj = scope_object_free;
	scope_handlers.clone_obj = NULL;
}
