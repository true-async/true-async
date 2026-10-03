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
#include "php_ini.h"
#include "ext/standard/info.h"
#include "Zend/zend_closures.h"
#include "php_true_async.h"
#include "coroutine.h"
#include "exceptions.h"
#include "scheduler.h"
#include "true_async_arginfo.h"

#ifdef TRUE_ASYNC_KNOWN_ANSWER
#include "known_answer.h"
#define TRUE_ASYNC_FUNCTIONS true_async_known_answer_functions
#else
#define TRUE_ASYNC_FUNCTIONS NULL
#endif

#ifdef TRUE_ASYNC_TEST_HOOKS
#include "test_hooks.h"
#endif

ZEND_DECLARE_MODULE_GLOBALS(true_async)

/* Off by default, as test_scheduler.enable: the scheduler slots are process-wide, and a binary
 * that loads the extension must still be able to run another provider. */
PHP_INI_BEGIN()
	PHP_INI_ENTRY("true_async.enable", "0", PHP_INI_SYSTEM, NULL)
	STD_PHP_INI_BOOLEAN("true_async.debug_deadlock",
						"1",
						PHP_INI_ALL,
						OnUpdateBool,
						debug_deadlock,
						zend_true_async_globals,
						true_async_globals)
PHP_INI_END()

static zend_class_entry *async_ce_awaitable = NULL;
static zend_class_entry *async_ce_completable = NULL;

/* False when the extension is disabled or the core refused its scheduler: RINIT and RSHUTDOWN do
 * nothing then. */
static bool scheduler_registered = false;

/* Only this extension's classes implement Awaitable: generic wait code reads an awaitable's memory
 * as a coroutine or an event (dev/plans/S3.md, section 13, bug 10). Completable extends it, so
 * this covers both. */
static int awaitable_gets_implemented(zend_class_entry *interface, zend_class_entry *class_entry)
{
	if (EXPECTED(class_entry->type == ZEND_INTERNAL_CLASS &&
				 class_entry->info.internal.module == &true_async_module_entry)) {
		return SUCCESS;
	}

	zend_error_noreturn(E_ERROR,
						"Class %s cannot implement interface %s: only the classes of true_async implement it",
						ZSTR_VAL(class_entry->name),
						ZSTR_VAL(interface->name));

	return FAILURE;
}

static PHP_GINIT_FUNCTION(true_async)
{
#if defined(ZTS) && defined(COMPILE_DL_TRUE_ASYNC)
	ZEND_TSRMLS_CACHE_UPDATE();
#endif
	memset(true_async_globals, 0, sizeof(*true_async_globals));
}

static PHP_MINIT_FUNCTION(true_async)
{
	REGISTER_INI_ENTRIES();

	/* A disabled extension registers no classes, as it registers no scheduler. */
	if (!zend_ini_parse_bool(zend_ini_str(ZEND_STRL("true_async.enable"), false))) {
		return SUCCESS;
	}

	async_ce_awaitable = register_class_Async_Awaitable();
	async_ce_awaitable->interface_gets_implemented = awaitable_gets_implemented;
	async_ce_completable = register_class_Async_Completable(async_ce_awaitable);
	async_register_exceptions_ce();
	async_register_coroutine_ce(async_ce_completable);

	scheduler_registered = async_scheduler_register();

	/* Registered here, not in the module entry, so an extension that is disabled, or whose scheduler
	 * the core refused, has no Async\ functions. */
	if (UNEXPECTED(!scheduler_registered)) {
		return SUCCESS;
	}

	if (UNEXPECTED(zend_register_functions(NULL, ext_functions, NULL, type) == FAILURE)) {
		return FAILURE;
	}

#ifdef TRUE_ASYNC_TEST_HOOKS
	/* A second table beside TRUE_ASYNC_FUNCTIONS: the mull lane builds the known-answer functions
	 * and the hooks together. */
	if (UNEXPECTED(zend_register_functions(NULL, true_async_test_hooks_functions, NULL, type) == FAILURE)) {
		return FAILURE;
	}
#endif

	return SUCCESS;
}

static PHP_RINIT_FUNCTION(true_async)
{
#if defined(ZTS) && defined(COMPILE_DL_TRUE_ASYNC)
	ZEND_TSRMLS_CACHE_UPDATE();
#endif

	if (scheduler_registered) {
		async_scheduler_request_startup();
	}

#ifdef TRUE_ASYNC_TEST_HOOKS
	/* A fault armed and never reached stays with the request that armed it. */
	ASYNC_G(fault_site) = ASYNC_TEST_FAULT_NONE;
#endif

	return SUCCESS;
}

static PHP_RSHUTDOWN_FUNCTION(true_async)
{
	if (scheduler_registered) {
		async_scheduler_request_shutdown();
	}

	return SUCCESS;
}

static PHP_MSHUTDOWN_FUNCTION(true_async)
{
	UNREGISTER_INI_ENTRIES();

	return SUCCESS;
}

static PHP_MINFO_FUNCTION(true_async)
{
	php_info_print_table_start();
	php_info_print_table_row(2, "true_async support", "enabled");
	php_info_print_table_row(2, "Version", PHP_TRUE_ASYNC_VERSION);
	php_info_print_table_end();

	DISPLAY_INI_ENTRIES();
}

///////////////////////////////////////////////////////////////////
/// Functions
///////////////////////////////////////////////////////////////////

/* Refuses while no scheduler runs (php -r launches none; after the request's last drain the core
 * turns async off) and in scheduler context, as TrueAsync. */
#define THROW_IF_UNAVAILABLE() \
	do { \
		if (UNEXPECTED(!ZEND_ASYNC_IS_ACTIVE)) { \
			zend_throw_error(NULL, "The operation cannot be executed while async is off"); \
			RETURN_THROWS(); \
		} \
\
		if (UNEXPECTED(ZEND_ASYNC_IN_SCHEDULER_CONTEXT)) { \
			zend_throw_error(NULL, "The operation cannot be executed in the scheduler context"); \
			RETURN_THROWS(); \
		} \
	} while (0)

ZEND_FUNCTION(Async_spawn)
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

	async_coroutine_t *coroutine = async_coroutine_new();

	/* ZEND_ASYNC_FCALL_DEFINE into the coroutine's own block. */
	zend_fcall_t *fcall = &coroutine->spawn_fcall;
	fcall->fci = fci;
	fcall->fci_cache = fcc;

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

	/* A CREATED coroutine is refused only when the scheduler coroutine cannot get a stack: the
	 * coroutine then never existed. */
	if (UNEXPECTED(!async_scheduler_enqueue(&coroutine->coroutine, NULL, false))) {
		OBJ_RELEASE(&coroutine->std);
		RETURN_THROWS();
	}

	RETURN_OBJ_COPY(&coroutine->std);
}

/* Waits for a coroutine (S3.md 4.1 and 4.8): the result and the exception are read from the finished
 * coroutine in place, as TrueAsync replays a finished coroutine (coroutine.c:1040-1068). */
ZEND_FUNCTION(Async_await)
{
	zend_object *awaitable = NULL;

	THROW_IF_UNAVAILABLE();

	ZEND_PARSE_PARAMETERS_START(1, 1)
		Z_PARAM_OBJ_OF_CLASS(awaitable, async_ce_completable)
	ZEND_PARSE_PARAMETERS_END();

	/* Coroutine is the only Completable until events come (S4). */
	ZEND_ASSERT(awaitable->ce == async_ce_coroutine);
	async_coroutine_t *target = async_coroutine_from_object(awaitable);

	if (UNEXPECTED(!async_await_coroutine(target))) {
		RETURN_THROWS();
	}

	zend_object *exception = target->coroutine.exception;

	if (UNEXPECTED(exception != NULL)) {
		GC_ADDREF(exception);
		zend_throw_exception_internal(exception);
		RETURN_THROWS();
	}

	if (Z_ISUNDEF(target->coroutine.result)) {
		RETURN_NULL();
	}

	RETURN_COPY_DEREF(&target->coroutine.result);
}

/* A yield (S3.md 4.1, TrueAsync's async.c:223-235): refused before the enqueue, so a refusal leaves the coroutine
 * running; otherwise it goes to the back of the run queue and parks there until its turn (D6). With
 * async off it does nothing, as in TrueAsync. */
ZEND_FUNCTION(Async_suspend)
{
	ZEND_PARSE_PARAMETERS_NONE();

	if (UNEXPECTED(!ZEND_ASYNC_IS_ACTIVE)) {
		return;
	}

	THROW_IF_UNAVAILABLE();

	async_coroutine_t *coroutine = (async_coroutine_t *) ZEND_ASYNC_CURRENT_COROUTINE;

	/* A finished coroutine is still current while finalize releases what it held. */
	if (UNEXPECTED(coroutine == NULL || ZEND_COROUTINE_IS_FINISHED(&coroutine->coroutine))) {
		zend_throw_error(NULL, "Cannot switch coroutines in the current execution context");
		RETURN_THROWS();
	}

	if (UNEXPECTED(!async_scheduler_enqueue(&coroutine->coroutine, NULL, false))) {
		RETURN_THROWS();
	}

	ZEND_ASYNC_SUSPEND();
}

/* S3.md section 6 and D7: the request that arrives inside waits in deferred_cancellation. Only the
 * outermost protect() ends the protection, so a nested one does not throw in the middle of the outer
 * (section 13, bug 1). Without a current coroutine (async off) the closure is just called. */
ZEND_FUNCTION(Async_protect)
{
	zend_object *closure = NULL;

	ZEND_PARSE_PARAMETERS_START(1, 1)
		Z_PARAM_OBJ_OF_CLASS(closure, zend_ce_closure)
	ZEND_PARSE_PARAMETERS_END();

	async_coroutine_t *coroutine = (async_coroutine_t *) ZEND_ASYNC_CURRENT_COROUTINE;
	const bool was_protected = coroutine != NULL && (coroutine->coroutine.flags & ASYNC_COROUTINE_F_PROTECTED);

	if (EXPECTED(coroutine != NULL)) {
		coroutine->coroutine.flags |= ASYNC_COROUTINE_F_PROTECTED;
	}

	zval closure_zval;
	ZVAL_OBJ(&closure_zval, closure);
	call_user_function(NULL, NULL, &closure_zval, return_value, 0, NULL);

	if (UNEXPECTED(Z_ISUNDEF_P(return_value))) {
		ZVAL_NULL(return_value);
	}

	if (UNEXPECTED(coroutine == NULL || was_protected)) {
		return;
	}

	coroutine->coroutine.flags &= ~ASYNC_COROUTINE_F_PROTECTED;

	zend_object *deferred_cancellation = coroutine->deferred_cancellation;

	if (UNEXPECTED(deferred_cancellation != NULL)) {
		coroutine->deferred_cancellation = NULL;
		ZEND_COROUTINE_SET_CANCELLED(&coroutine->coroutine);
		zend_throw_exception_internal(deferred_cancellation);
	}
}

ZEND_FUNCTION(Async_current_coroutine)
{
	THROW_IF_UNAVAILABLE();

	ZEND_PARSE_PARAMETERS_NONE();

	zend_coroutine_t *coroutine = ZEND_ASYNC_CURRENT_COROUTINE;

	if (UNEXPECTED(coroutine == NULL)) {
		zend_throw_exception(async_ce_async_exception, "The current coroutine is not defined", 0);
		RETURN_THROWS();
	}

	RETURN_OBJ_COPY(ZEND_COROUTINE_OBJECT(coroutine));
}

ZEND_FUNCTION(Async_get_coroutines)
{
	THROW_IF_UNAVAILABLE();

	ZEND_PARSE_PARAMETERS_NONE();

	array_init_size(return_value, zend_hash_num_elements(&ASYNC_G(coroutines)));

	async_coroutine_t *coroutine = NULL;

	ZEND_HASH_FOREACH_PTR(&ASYNC_G(coroutines), coroutine)
	{
		GC_ADDREF(&coroutine->std);
		add_next_index_object(return_value, &coroutine->std);
	}
	ZEND_HASH_FOREACH_END();
}

/* TrueAsync's async.c:947-960: refused while async is off and in scheduler context. */
ZEND_FUNCTION(Async_graceful_shutdown)
{
	zend_object *cancellation = NULL;

	ZEND_PARSE_PARAMETERS_START(0, 1)
		Z_PARAM_OPTIONAL
		Z_PARAM_OBJ_OF_CLASS_OR_NULL(cancellation, async_ce_cancellation)
	ZEND_PARSE_PARAMETERS_END();

	THROW_IF_UNAVAILABLE();

	async_scheduler_graceful_shutdown(cancellation);
}

/* clang-format off */
zend_module_entry true_async_module_entry = {
	STANDARD_MODULE_HEADER,
	"true_async",
	TRUE_ASYNC_FUNCTIONS,
	PHP_MINIT(true_async),
	PHP_MSHUTDOWN(true_async),
	PHP_RINIT(true_async),
	PHP_RSHUTDOWN(true_async),
	PHP_MINFO(true_async),
	PHP_TRUE_ASYNC_VERSION,
	PHP_MODULE_GLOBALS(true_async),
	PHP_GINIT(true_async),
	NULL,
	NULL,
	STANDARD_MODULE_PROPERTIES_EX
};
/* clang-format on */

#ifdef COMPILE_DL_TRUE_ASYNC
#ifdef ZTS
ZEND_TSRMLS_CACHE_DEFINE()
#endif
ZEND_GET_MODULE(true_async)
#endif
