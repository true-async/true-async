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
#include "exceptions.h"
#include "context.h"
#include "scope.h"
#include "context_arginfo.h"

typedef struct
{
	/* The scope whose context this is, borrowed: the scope holds the context and its free clears this.
	 * NULL for a coroutine's context and for `new Context()`. */
	async_scope_t *scope;
	zend_async_context_t context; /* std last */
} async_context_t;

static zend_always_inline async_context_t *async_context_from_object(zend_object *object)
{
	return (async_context_t *) ((char *) object - offsetof(async_context_t, context.std));
}

zend_class_entry *async_ce_context = NULL;
zend_class_entry *async_ce_context_exception = NULL;

static zend_object_handlers context_handlers;

static zend_object *context_object_create(zend_class_entry *class_entry)
{
	async_context_t *context = zend_object_alloc(sizeof(async_context_t), class_entry);

	context->scope = NULL;
	zend_async_context_tables_init(&context->context);
	zend_object_std_init(&context->context.std, class_entry);
	object_properties_init(&context->context.std, class_entry);

	return &context->context.std;
}

zend_object *async_context_new(void)
{
	return context_object_create(async_ce_context);
}

zend_object *async_context_new_for_scope(async_scope_t *scope)
{
	zend_object *object = context_object_create(async_ce_context);

	async_context_from_object(object)->scope = scope;

	return object;
}

void async_context_detach_scope(zend_object *object)
{
	async_context_from_object(object)->scope = NULL;
}

/* The tables go after zend_object_std_dtor, which clears the WeakReferences: a value's destructor that
 * reaches the Context through one would write into a table being destroyed. Not in dtor_obj: a
 * destructor that runs later at shutdown would find them destroyed. */
static void context_object_free(zend_object *object)
{
	zend_object_std_dtor(object);
	zend_async_context_tables_destroy(ZEND_ASYNC_CONTEXT_FROM_OBJ(object));
}

static HashTable *context_object_get_gc(zend_object *object, zval **table, int *num)
{
	zend_get_gc_buffer *buffer = zend_get_gc_buffer_create();

	zend_async_context_entry_gc(ZEND_ASYNC_CONTEXT_FROM_OBJ(object), buffer);
	zend_get_gc_buffer_use(buffer, table, num);

	return NULL;
}

///////////////////////////////////////////////////////////////////
/// Methods
///////////////////////////////////////////////////////////////////

/* The key is parsed as any value: Z_PARAM_OBJ_OR_STR would turn an int into a string in weak mode, and
 * TrueAsync refuses it (context/004). */
#define THROW_IF_INVALID_KEY(key) \
	do { \
		if (UNEXPECTED(Z_TYPE_P(key) != IS_STRING && Z_TYPE_P(key) != IS_OBJECT)) { \
			zend_argument_type_error(1, "must be of type string|object, %s given", zend_zval_type_name(key)); \
			RETURN_THROWS(); \
		} \
	} while (0)

/* The value this Context holds under a string or object key, NULL if none. */
static zval *context_find_local(zend_object *object, const zval *key)
{
	zend_async_context_t *context = ZEND_ASYNC_CONTEXT_FROM_OBJ(object);

	if (Z_TYPE_P(key) == IS_STRING) {
		return zend_async_context_entry_find(context, Z_STR_P(key), NULL);
	}

	return zend_async_context_entry_find(context, NULL, Z_OBJ_P(key));
}

/* The value at the nearest level: this Context, then the context of each scope above its scope, up to a
 * scope with no parent. A scope without a context is skipped: one is made only when asked for (TrueAsync's
 * async_context_find, context.c:26-71). A loop, so a deep chain of scopes costs no C stack. */
static zval *context_find(zend_object *object, const zval *key)
{
	zval *value = context_find_local(object, key);

	if (value != NULL) {
		return value;
	}

	const async_scope_t *scope = async_context_from_object(object)->scope;

	if (scope == NULL) {
		return NULL;
	}

	for (scope = scope->parent_scope; scope != NULL; scope = scope->parent_scope) {
		if (scope->context != NULL) {
			value = context_find_local(scope->context, key);

			if (value != NULL) {
				return value;
			}
		}
	}

	return NULL;
}

static ZEND_COLD void context_throw_missing_key(const zval *key)
{
	if (Z_TYPE_P(key) == IS_STRING) {
		zend_throw_exception_ex(async_ce_context_exception, 0, "Context key \"%s\" not found", Z_STRVAL_P(key));
		return;
	}

	zend_throw_exception_ex(
			async_ce_context_exception, 0, "Context key of type %s not found", ZSTR_VAL(Z_OBJCE_P(key)->name));
}

#define CONTEXT_METHOD_KEY(key) \
	ZEND_PARSE_PARAMETERS_START(1, 1) \
		Z_PARAM_ZVAL(key) \
	ZEND_PARSE_PARAMETERS_END(); \
	THROW_IF_INVALID_KEY(key)

ZEND_METHOD(Async_Context, find)
{
	zval *key;
	CONTEXT_METHOD_KEY(key);

	const zval *value = context_find(Z_OBJ_P(ZEND_THIS), key);

	if (value == NULL) {
		RETURN_NULL();
	}

	RETURN_COPY(value);
}

ZEND_METHOD(Async_Context, get)
{
	zval *key;
	CONTEXT_METHOD_KEY(key);

	const zval *value = context_find(Z_OBJ_P(ZEND_THIS), key);

	if (UNEXPECTED(value == NULL)) {
		context_throw_missing_key(key);
		RETURN_THROWS();
	}

	RETURN_COPY(value);
}

ZEND_METHOD(Async_Context, has)
{
	zval *key;
	CONTEXT_METHOD_KEY(key);

	RETURN_BOOL(context_find(Z_OBJ_P(ZEND_THIS), key) != NULL);
}

ZEND_METHOD(Async_Context, findLocal)
{
	zval *key;
	CONTEXT_METHOD_KEY(key);

	const zval *value = context_find_local(Z_OBJ_P(ZEND_THIS), key);

	if (value == NULL) {
		RETURN_NULL();
	}

	RETURN_COPY(value);
}

ZEND_METHOD(Async_Context, getLocal)
{
	zval *key;
	CONTEXT_METHOD_KEY(key);

	const zval *value = context_find_local(Z_OBJ_P(ZEND_THIS), key);

	if (UNEXPECTED(value == NULL)) {
		context_throw_missing_key(key);
		RETURN_THROWS();
	}

	RETURN_COPY(value);
}

ZEND_METHOD(Async_Context, hasLocal)
{
	zval *key;
	CONTEXT_METHOD_KEY(key);

	RETURN_BOOL(context_find_local(Z_OBJ_P(ZEND_THIS), key) != NULL);
}

ZEND_METHOD(Async_Context, set)
{
	zval *key;
	zval *value;
	bool replace = false;

	ZEND_PARSE_PARAMETERS_START(2, 3)
		Z_PARAM_ZVAL(key)
		Z_PARAM_ZVAL(value)
		Z_PARAM_OPTIONAL
		Z_PARAM_BOOL(replace)
	ZEND_PARSE_PARAMETERS_END();

	THROW_IF_INVALID_KEY(key);

	zend_object *object = Z_OBJ_P(ZEND_THIS);

	if (UNEXPECTED(!replace && context_find_local(object, key) != NULL)) {
		zend_throw_exception(async_ce_async_exception, "Context key already exists and replace is false", 0);
		RETURN_THROWS();
	}

	zend_async_context_t *context = ZEND_ASYNC_CONTEXT_FROM_OBJ(object);

	if (Z_TYPE_P(key) == IS_STRING) {
		zend_async_context_entry_set(context, Z_STR_P(key), NULL, value);
	} else {
		zend_async_context_entry_set(context, NULL, Z_OBJ_P(key), value);
	}

	RETURN_OBJ_COPY(object);
}

ZEND_METHOD(Async_Context, unset)
{
	zval *key;
	CONTEXT_METHOD_KEY(key);

	zend_object *object = Z_OBJ_P(ZEND_THIS);
	zend_async_context_t *context = ZEND_ASYNC_CONTEXT_FROM_OBJ(object);

	if (Z_TYPE_P(key) == IS_STRING) {
		zend_async_context_entry_unset(context, Z_STR_P(key), NULL);
	} else {
		zend_async_context_entry_unset(context, NULL, Z_OBJ_P(key));
	}

	RETURN_OBJ_COPY(object);
}

void async_register_context_ce(void)
{
	async_ce_context_exception = register_class_Async_ContextException(async_ce_async_exception);
	async_ce_context = register_class_Async_Context();
	async_ce_context->create_object = context_object_create;
	async_ce_context->default_object_handlers = &context_handlers;

	memcpy(&context_handlers, zend_get_std_object_handlers(), sizeof(zend_object_handlers));
	context_handlers.offset = offsetof(async_context_t, context.std);
	context_handlers.free_obj = context_object_free;
	context_handlers.get_gc = context_object_get_gc;
	context_handlers.clone_obj = NULL;
}
