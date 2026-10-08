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
#include "collector.h"
#include "context.h"
#include "exceptions.h"
#include "await.h"
#include "future.h"
#include "scheduler.h"
#include "scope.h"
#include "timeout.h"
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

static ZEND_INI_MH(OnUpdatePartialDeadlock)
{
	if (zend_string_equals_literal_ci(new_value, "report")) {
		ASYNC_G(partial_deadlock) = ASYNC_PARTIAL_DEADLOCK_REPORT;
	} else if (zend_string_equals_literal_ci(new_value, "cancel")) {
		ASYNC_G(partial_deadlock) = ASYNC_PARTIAL_DEADLOCK_CANCEL;
	} else if (ZSTR_LEN(new_value) == 0 || zend_string_equals_literal(new_value, "0") ||
			   zend_string_equals_literal_ci(new_value, "off")) {
		/* php.ini and -d read a bare off as a boolean: the empty string. */
		ASYNC_G(partial_deadlock) = ASYNC_PARTIAL_DEADLOCK_OFF;
	} else {
		return FAILURE;
	}

	return SUCCESS;
}

static ZEND_INI_MH(OnUpdatePartialDeadlockInterval)
{
	zend_string *error = NULL;
	const zend_long interval = zend_ini_parse_quantity(new_value, &error);

	if (UNEXPECTED(error != NULL)) {
		zend_string_release(error);
		return FAILURE;
	}

	/* The parser reads an empty value as 0, and php.ini turns a bare `off` into one: only a literal 0
	 * walks at every idle point. */
	if (interval == 0 && !zend_string_equals_literal(new_value, "0")) {
		return FAILURE;
	}

	if (interval != 0 && (interval < ASYNC_COLLECTOR_INTERVAL_MIN || interval > ASYNC_COLLECTOR_INTERVAL_MAX)) {
		return FAILURE;
	}

	ASYNC_G(partial_deadlock_interval) = interval;

	return SUCCESS;
}

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
	PHP_INI_ENTRY("true_async.partial_deadlock", "report", PHP_INI_ALL, OnUpdatePartialDeadlock)
	PHP_INI_ENTRY("true_async.partial_deadlock_interval", "5000", PHP_INI_ALL, OnUpdatePartialDeadlockInterval)
PHP_INI_END()

zend_class_entry *async_ce_awaitable = NULL;
zend_class_entry *async_ce_completable = NULL;

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
	true_async_globals->wake_pair.read_fd = SOCK_ERR;
	true_async_globals->wake_pair.write_fd = SOCK_ERR;
}

static PHP_GSHUTDOWN_FUNCTION(true_async)
{
	async_wake_pair_close(&true_async_globals->wake_pair);
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
	async_ce_signal = register_class_Async_Signal();

	scheduler_registered = async_scheduler_register();

	/* Registered here, not in the module entry, so an extension that is disabled, or whose scheduler
	 * the core refused, has no Async\ functions. Futures and timeouts are left out with them: user
	 * code can create a Future without a function, and its callbacks run in coroutines that need
	 * this scheduler. */
	if (UNEXPECTED(!scheduler_registered)) {
		return SUCCESS;
	}

	async_register_future_ce(async_ce_completable);
	async_register_timeout_ce(async_ce_completable);
	async_register_scope_ce();
	async_register_context_ce();
	zend_async_new_context_fn = async_context_new;

	if (UNEXPECTED(zend_register_functions(NULL, ext_functions, NULL, type) == FAILURE)) {
		return FAILURE;
	}

#ifdef TRUE_ASYNC_TEST_HOOKS
	/* A second table beside TRUE_ASYNC_FUNCTIONS: the mull lane builds the known-answer functions
	 * and the hooks together. */
	if (UNEXPECTED(zend_register_functions(NULL, true_async_test_hooks_functions, NULL, type) == FAILURE)) {
		return FAILURE;
	}

	async_test_hooks_register_classes();
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
		async_scope_request_startup();
		async_reactor_request_startup();
		async_io_provider_request_startup();
		ASYNC_G(signals) = NULL;
		async_collector_request_startup();
	}

#ifdef TRUE_ASYNC_TEST_HOOKS
	/* A fault armed and never reached stays with the request that armed it. */
	ASYNC_G(fault_site) = ASYNC_TEST_FAULT_NONE;
	ASYNC_G(test_block_releases) = 0;
	ASYNC_G(test_typed_unlinks) = 0;
	ASYNC_G(test_aborts) = 0;
	ASYNC_G(test_exit_deadline_ms) = 0;
	ASYNC_G(test_trigger) = NULL;
	ASYNC_G(test_firer) = NULL;
	ASYNC_G(test_print_at_teardown) = false;
#endif

	return SUCCESS;
}

static PHP_RSHUTDOWN_FUNCTION(true_async)
{
	if (scheduler_registered) {
		zend_array *released_values = NULL;

		async_scheduler_request_shutdown(&released_values);
#ifdef TRUE_ASYNC_TEST_HOOKS
		async_test_hooks_request_shutdown();
#endif
		async_io_provider_request_shutdown();
#ifndef PHP_WIN32
		async_signal_request_shutdown();
#endif
		async_reactor_request_shutdown();

#ifdef TRUE_ASYNC_TEST_HOOKS
		if (UNEXPECTED(ASYNC_G(test_print_at_teardown))) {
			php_printf("teardown: done\n");
		}
#endif

		/* Last: a destructor that throws here bails out of the rest of this function. */
		zend_array_release(released_values);
	}

	return SUCCESS;
}

static PHP_MSHUTDOWN_FUNCTION(true_async)
{
	UNREGISTER_INI_ENTRIES();

	/* The core's unregister leaves the slot, and the factory goes with this module's code. */
	if (zend_async_new_context_fn == async_context_new) {
		zend_async_new_context_fn = NULL;
	}

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

	async_coroutine_t *coroutine =
			async_scope_spawn(async_scope_current(), NULL, &fci, &fcc, args, args_count, named_args);

	if (UNEXPECTED(coroutine == NULL)) {
		RETURN_THROWS();
	}

	RETURN_OBJ(&coroutine->std);
}

/* Waits for a coroutine (S3.md 4.1 and 4.8) or a Future (S5.md section 4): the result and the
 * exception are read from the finished target in place, as TrueAsync replays a finished coroutine
 * (coroutine.c:1040-1068). */
ZEND_FUNCTION(Async_await)
{
	zend_object *awaitable = NULL;
	zend_object *cancellation = NULL;

	THROW_IF_UNAVAILABLE();

	ZEND_PARSE_PARAMETERS_START(1, 2)
		Z_PARAM_OBJ_OF_CLASS(awaitable, async_ce_completable)
		Z_PARAM_OPTIONAL
		Z_PARAM_OBJ_OF_CLASS_OR_NULL(cancellation, async_ce_completable)
	ZEND_PARSE_PARAMETERS_END();

	if (UNEXPECTED(awaitable->ce == async_ce_timeout)) {
		zend_throw_error(NULL, "Async\\Timeout can only be used as a cancellation token");
		RETURN_THROWS();
	}

	async_awaitable_t *target_awaitable = async_await_awaitable_of(awaitable);

	if (UNEXPECTED(target_awaitable == NULL)) {
		RETURN_THROWS();
	}

	/* A Future is marked observed on entry, before its token is read, as in TrueAsync (async.c:319-320;
	 * dev/plans/S5.md, section 4). */
	if (awaitable->ce == async_ce_future) {
		((async_event_t *) target_awaitable)->flags |= ASYNC_EVENT_F_RESULT_USED | ASYNC_EVENT_F_EXC_CAUGHT;
	}

	async_awaitable_t *token = NULL;

	if (cancellation != NULL) {
		token = async_await_awaitable_of(cancellation);

		if (UNEXPECTED(token == NULL)) {
			RETURN_THROWS();
		}

		/* The awaitable as its own token: no token (async.c:322-325). */
		if (token == target_awaitable) {
			token = NULL;
		}
	}

	/* The wait's own reference: the token's Future object may let go of its event meanwhile (a
	 * second __construct()). */
	if (token != NULL) {
		async_awaitable_addref(token);
	}

	bool coroutine_finished = false;

	if (awaitable->ce == async_ce_future) {
		async_future_await((async_future_event_t *) target_awaitable, return_value, token);
	} else {
		ZEND_ASSERT(awaitable->ce == async_ce_coroutine);
		coroutine_finished = async_await_coroutine((async_coroutine_t *) target_awaitable, token);
	}

	if (token != NULL) {
		async_awaitable_release(token);
	}

	if (!coroutine_finished) {
		return;
	}

	async_coroutine_t *target = (async_coroutine_t *) target_awaitable;
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

	THROW_IF_SCHEDULER_CONTEXT();

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

/* TrueAsync's async.c:672-699 (dev/plans/S4.md section 1): 0 is a yield; a negative `ms` is refused,
 * where TrueAsync arms a timer of about 49 days. With no current coroutine (async off) it returns at
 * once. */
ZEND_FUNCTION(Async_delay)
{
	zend_long ms;

	ZEND_PARSE_PARAMETERS_START(1, 1)
		Z_PARAM_LONG(ms)
	ZEND_PARSE_PARAMETERS_END();

	if (UNEXPECTED(ms < 0)) {
		zend_argument_value_error(1, "must be greater than or equal to 0");
		RETURN_THROWS();
	}

	async_coroutine_t *coroutine = (async_coroutine_t *) ZEND_ASYNC_CURRENT_COROUTINE;

	if (UNEXPECTED(coroutine == NULL)) {
		return;
	}

	THROW_IF_SCHEDULER_CONTEXT();

	/* A finished coroutine is still current while finalize releases what it held. */
	if (UNEXPECTED(ZEND_COROUTINE_IS_FINISHED(&coroutine->coroutine))) {
		zend_throw_error(NULL, "Cannot switch coroutines in the current execution context");
		RETURN_THROWS();
	}

	if (ms == 0) {
		if (EXPECTED(async_scheduler_enqueue(&coroutine->coroutine, NULL, false))) {
			ZEND_ASYNC_SUSPEND();
		}

		return;
	}

	async_reactor_delay(coroutine, ms);
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

	/* An exit unwinding the closure (D16's, or a dropped Fiber's) stays the exception: chained under a
	 * cancellation it would be released, and a catch would stop the unwind. */
	if (UNEXPECTED(deferred_cancellation != NULL && EG(exception) != NULL &&
				   (zend_is_graceful_exit(EG(exception)) || zend_is_unwind_exit(EG(exception))))) {
		coroutine->deferred_cancellation = NULL;
		OBJ_RELEASE(deferred_cancellation);
		return;
	}

	if (UNEXPECTED(deferred_cancellation != NULL)) {
		coroutine->deferred_cancellation = NULL;
		ZEND_COROUTINE_SET_CANCELLED(&coroutine->coroutine);
		zend_throw_exception_internal(deferred_cancellation);
	}
}

/* The current coroutine, NULL if none or if its object is being freed: free_obj of a finished coroutine
 * runs PHP code (a WeakMap value's destructor) while it is still current, and the engine frees the
 * object after free_obj whatever its refcount, so a reference taken then, or a context made for it,
 * would outlive it. */
static zend_coroutine_t *async_current_coroutine_alive(void)
{
	zend_coroutine_t *coroutine = ZEND_ASYNC_CURRENT_COROUTINE;

	if (UNEXPECTED(coroutine != NULL && (OBJ_FLAGS(ZEND_COROUTINE_OBJECT(coroutine)) & IS_OBJ_FREE_CALLED))) {
		return NULL;
	}

	return coroutine;
}

ZEND_FUNCTION(Async_current_coroutine)
{
	THROW_IF_UNAVAILABLE();

	ZEND_PARSE_PARAMETERS_NONE();

	zend_coroutine_t *coroutine = async_current_coroutine_alive();

	if (UNEXPECTED(coroutine == NULL)) {
		zend_throw_exception(async_ce_async_exception, "The current coroutine is not defined", 0);
		RETURN_THROWS();
	}

	RETURN_OBJ_COPY(ZEND_COROUTINE_OBJECT(coroutine));
}

ZEND_FUNCTION(Async_coroutine_context)
{
	THROW_IF_UNAVAILABLE();

	ZEND_PARSE_PARAMETERS_NONE();

	zend_coroutine_t *coroutine = async_current_coroutine_alive();

	if (UNEXPECTED(coroutine == NULL)) {
		zend_throw_exception(async_ce_async_exception, "The current coroutine is not defined", 0);
		RETURN_THROWS();
	}

	zend_object *context = zend_async_context_get(coroutine);
	ZEND_ASSERT(context != NULL && "a coroutine exists only while this extension's factory is set");

	RETURN_OBJ_COPY(context);
}

ZEND_FUNCTION(Async_current_context)
{
	THROW_IF_UNAVAILABLE();

	ZEND_PARSE_PARAMETERS_NONE();

	const zend_coroutine_t *coroutine = ZEND_ASYNC_CURRENT_COROUTINE;

	/* A finished coroutine is still current while it releases what it held, after it left its scope: the
	 * global scope's context would hand the code of another scope, a `new Scope()` included, the root
	 * values (TrueAsync throws on a NULL scope, async.c:811-813). One that left the global scope reads the
	 * root context. */
	if (UNEXPECTED(coroutine != NULL && (coroutine->flags & ASYNC_COROUTINE_F_LEFT_NON_GLOBAL_SCOPE) &&
				   ((const async_coroutine_t *) coroutine)->scope == NULL)) {
		zend_throw_exception(async_ce_async_exception, "The current scope is not defined", 0);
		RETURN_THROWS();
	}

	RETURN_OBJ_COPY(async_scope_context(async_scope_current()));
}

ZEND_FUNCTION(Async_root_context)
{
	THROW_IF_UNAVAILABLE();

	ZEND_PARSE_PARAMETERS_NONE();

	RETURN_OBJ_COPY(async_scope_context(ASYNC_G(global_scope)));
}

ZEND_FUNCTION(Async_request_context)
{
	THROW_IF_UNAVAILABLE();

	ZEND_PARSE_PARAMETERS_NONE();

	RETURN_NULL();
}

ZEND_FUNCTION(Async_get_coroutines)
{
	THROW_IF_UNAVAILABLE();

	ZEND_PARSE_PARAMETERS_NONE();

	array_init_size(return_value, zend_hash_num_elements(&ASYNC_G(coroutines)));

	async_coroutine_t *coroutine = NULL;

	ZEND_HASH_FOREACH_PTR(&ASYNC_G(coroutines), coroutine)
	{
		/* A core coroutine the scheduler refused to enqueue: it never runs (scheduler.h). */
		if (UNEXPECTED(ZEND_COROUTINE_STATUS(&coroutine->coroutine) == ZEND_COROUTINE_STATUS_CREATED)) {
			continue;
		}

		GC_ADDREF(&coroutine->std);
		add_next_index_object(return_value, &coroutine->std);
#ifdef TRUE_ASYNC_TEST_HOOKS
		coroutine->coroutine.flags |= ASYNC_COROUTINE_F_HANDED_OUT;
#endif
	}
	ZEND_HASH_FOREACH_END();
}

/* dev/plans/S7.md section 8: the walk runs in the calling coroutine, which runs and so holds whatever
 * its own stack holds. No policy applies to what it returns. */
ZEND_FUNCTION(Async_get_deadlocked_coroutines)
{
	THROW_IF_UNAVAILABLE();

	ZEND_PARSE_PARAMETERS_NONE();

	/* A forked child's first entry may be this call: the parent's timers would read as armed. */
	async_reactor_check_fork();

	uint32_t count = 0;
	async_coroutine_t **found = async_collector_find(&count, 0);

	array_init_size(return_value, count);

	for (uint32_t i = 0; i < count; i++) {
		GC_ADDREF(&found[i]->std);
		add_next_index_object(return_value, &found[i]->std);
#ifdef TRUE_ASYNC_TEST_HOOKS
		found[i]->coroutine.flags |= ASYNC_COROUTINE_F_HANDED_OUT;
#endif
	}

	if (EXPECTED(found != NULL)) {
		efree(found);
	}
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
	PHP_GSHUTDOWN(true_async),
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
