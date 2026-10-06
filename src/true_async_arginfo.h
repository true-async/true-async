/* This is a generated file, edit true_async.stub.php instead.
 * Stub hash: 46e7fed4c9195bfca6915f5c09913a40d67323ba */

#include "zend_enum.h"

ZEND_BEGIN_ARG_WITH_RETURN_OBJ_INFO_EX(arginfo_Async_spawn, 0, 1, Async\\Coroutine, 0)
	ZEND_ARG_TYPE_INFO(0, task, IS_CALLABLE, 0)
	ZEND_ARG_VARIADIC_TYPE_INFO(0, args, IS_MIXED, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_Async_await, 0, 1, IS_MIXED, 0)
	ZEND_ARG_OBJ_INFO(0, awaitable, Async\\Completable, 0)
	ZEND_ARG_OBJ_INFO_WITH_DEFAULT_VALUE(0, cancellation, Async\\Completable, 1, "null")
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_Async_await_any_or_fail, 0, 1, IS_MIXED, 0)
	ZEND_ARG_OBJ_TYPE_MASK(0, triggers, Traversable, MAY_BE_ARRAY, NULL)
	ZEND_ARG_OBJ_INFO_WITH_DEFAULT_VALUE(0, cancellation, Async\\Awaitable, 1, "null")
ZEND_END_ARG_INFO()

#define arginfo_Async_await_first_success arginfo_Async_await_any_or_fail

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_Async_await_all_or_fail, 0, 1, IS_ARRAY, 0)
	ZEND_ARG_OBJ_TYPE_MASK(0, triggers, Traversable, MAY_BE_ARRAY, NULL)
	ZEND_ARG_OBJ_INFO_WITH_DEFAULT_VALUE(0, cancellation, Async\\Awaitable, 1, "null")
	ZEND_ARG_TYPE_INFO_WITH_DEFAULT_VALUE(0, preserveKeyOrder, _IS_BOOL, 0, "true")
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_Async_await_all, 0, 1, IS_ARRAY, 0)
	ZEND_ARG_OBJ_TYPE_MASK(0, triggers, Traversable, MAY_BE_ARRAY, NULL)
	ZEND_ARG_OBJ_INFO_WITH_DEFAULT_VALUE(0, cancellation, Async\\Awaitable, 1, "null")
	ZEND_ARG_TYPE_INFO_WITH_DEFAULT_VALUE(0, preserveKeyOrder, _IS_BOOL, 0, "true")
	ZEND_ARG_TYPE_INFO_WITH_DEFAULT_VALUE(0, fillNull, _IS_BOOL, 0, "false")
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_Async_await_any_of_or_fail, 0, 2, IS_ARRAY, 0)
	ZEND_ARG_TYPE_INFO(0, count, IS_LONG, 0)
	ZEND_ARG_OBJ_TYPE_MASK(0, triggers, Traversable, MAY_BE_ARRAY, NULL)
	ZEND_ARG_OBJ_INFO_WITH_DEFAULT_VALUE(0, cancellation, Async\\Awaitable, 1, "null")
	ZEND_ARG_TYPE_INFO_WITH_DEFAULT_VALUE(0, preserveKeyOrder, _IS_BOOL, 0, "true")
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_Async_await_any_of, 0, 2, IS_ARRAY, 0)
	ZEND_ARG_TYPE_INFO(0, count, IS_LONG, 0)
	ZEND_ARG_OBJ_TYPE_MASK(0, triggers, Traversable, MAY_BE_ARRAY, NULL)
	ZEND_ARG_OBJ_INFO_WITH_DEFAULT_VALUE(0, cancellation, Async\\Awaitable, 1, "null")
	ZEND_ARG_TYPE_INFO_WITH_DEFAULT_VALUE(0, preserveKeyOrder, _IS_BOOL, 0, "true")
	ZEND_ARG_TYPE_INFO_WITH_DEFAULT_VALUE(0, fillNull, _IS_BOOL, 0, "false")
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_OBJ_INFO_EX(arginfo_Async_timeout, 0, 1, Async\\Awaitable, 0)
	ZEND_ARG_TYPE_INFO(0, ms, IS_LONG, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_OBJ_INFO_EX(arginfo_Async_signal, 0, 1, Async\\Future, 0)
	ZEND_ARG_OBJ_INFO(0, signal, Async\\Signal, 0)
	ZEND_ARG_OBJ_INFO_WITH_DEFAULT_VALUE(0, cancellation, Async\\Completable, 1, "null")
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_Async_suspend, 0, 0, IS_VOID, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_Async_delay, 0, 1, IS_VOID, 0)
	ZEND_ARG_TYPE_INFO(0, ms, IS_LONG, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_Async_protect, 0, 1, IS_MIXED, 0)
	ZEND_ARG_OBJ_INFO(0, closure, Closure, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_OBJ_INFO_EX(arginfo_Async_current_coroutine, 0, 0, Async\\Coroutine, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_Async_get_coroutines, 0, 0, IS_ARRAY, 0)
ZEND_END_ARG_INFO()

#define arginfo_Async_get_deadlocked_coroutines arginfo_Async_get_coroutines

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_Async_graceful_shutdown, 0, 0, IS_VOID, 0)
	ZEND_ARG_OBJ_INFO_WITH_DEFAULT_VALUE(0, cancellationError, Async\\AsyncCancellation, 1, "null")
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_class_Async_Completable_cancel, 0, 0, IS_VOID, 0)
	ZEND_ARG_OBJ_INFO_WITH_DEFAULT_VALUE(0, cancellation, Async\\AsyncCancellation, 1, "null")
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_class_Async_Completable_isCompleted, 0, 0, _IS_BOOL, 0)
ZEND_END_ARG_INFO()

#define arginfo_class_Async_Completable_isCancelled arginfo_class_Async_Completable_isCompleted

ZEND_FUNCTION(Async_spawn);
ZEND_FUNCTION(Async_await);
ZEND_FUNCTION(Async_await_any_or_fail);
ZEND_FUNCTION(Async_await_first_success);
ZEND_FUNCTION(Async_await_all_or_fail);
ZEND_FUNCTION(Async_await_all);
ZEND_FUNCTION(Async_await_any_of_or_fail);
ZEND_FUNCTION(Async_await_any_of);
ZEND_FUNCTION(Async_timeout);
ZEND_FUNCTION(Async_signal);
ZEND_FUNCTION(Async_suspend);
ZEND_FUNCTION(Async_delay);
ZEND_FUNCTION(Async_protect);
ZEND_FUNCTION(Async_current_coroutine);
ZEND_FUNCTION(Async_get_coroutines);
ZEND_FUNCTION(Async_get_deadlocked_coroutines);
ZEND_FUNCTION(Async_graceful_shutdown);

static const zend_function_entry ext_functions[] = {
	ZEND_RAW_FENTRY(ZEND_NS_NAME("Async", "spawn"), zif_Async_spawn, arginfo_Async_spawn, 0, NULL, NULL)
	ZEND_RAW_FENTRY(ZEND_NS_NAME("Async", "await"), zif_Async_await, arginfo_Async_await, 0, NULL, NULL)
	ZEND_RAW_FENTRY(ZEND_NS_NAME("Async", "await_any_or_fail"), zif_Async_await_any_or_fail, arginfo_Async_await_any_or_fail, 0, NULL, NULL)
	ZEND_RAW_FENTRY(ZEND_NS_NAME("Async", "await_first_success"), zif_Async_await_first_success, arginfo_Async_await_first_success, 0, NULL, NULL)
	ZEND_RAW_FENTRY(ZEND_NS_NAME("Async", "await_all_or_fail"), zif_Async_await_all_or_fail, arginfo_Async_await_all_or_fail, 0, NULL, NULL)
	ZEND_RAW_FENTRY(ZEND_NS_NAME("Async", "await_all"), zif_Async_await_all, arginfo_Async_await_all, 0, NULL, NULL)
	ZEND_RAW_FENTRY(ZEND_NS_NAME("Async", "await_any_of_or_fail"), zif_Async_await_any_of_or_fail, arginfo_Async_await_any_of_or_fail, 0, NULL, NULL)
	ZEND_RAW_FENTRY(ZEND_NS_NAME("Async", "await_any_of"), zif_Async_await_any_of, arginfo_Async_await_any_of, 0, NULL, NULL)
	ZEND_RAW_FENTRY(ZEND_NS_NAME("Async", "timeout"), zif_Async_timeout, arginfo_Async_timeout, 0, NULL, NULL)
	ZEND_RAW_FENTRY(ZEND_NS_NAME("Async", "signal"), zif_Async_signal, arginfo_Async_signal, 0, NULL, NULL)
	ZEND_RAW_FENTRY(ZEND_NS_NAME("Async", "suspend"), zif_Async_suspend, arginfo_Async_suspend, 0, NULL, NULL)
	ZEND_RAW_FENTRY(ZEND_NS_NAME("Async", "delay"), zif_Async_delay, arginfo_Async_delay, 0, NULL, NULL)
	ZEND_RAW_FENTRY(ZEND_NS_NAME("Async", "protect"), zif_Async_protect, arginfo_Async_protect, 0, NULL, NULL)
	ZEND_RAW_FENTRY(ZEND_NS_NAME("Async", "current_coroutine"), zif_Async_current_coroutine, arginfo_Async_current_coroutine, 0, NULL, NULL)
	ZEND_RAW_FENTRY(ZEND_NS_NAME("Async", "get_coroutines"), zif_Async_get_coroutines, arginfo_Async_get_coroutines, 0, NULL, NULL)
	ZEND_RAW_FENTRY(ZEND_NS_NAME("Async", "get_deadlocked_coroutines"), zif_Async_get_deadlocked_coroutines, arginfo_Async_get_deadlocked_coroutines, 0, NULL, NULL)
	ZEND_RAW_FENTRY(ZEND_NS_NAME("Async", "graceful_shutdown"), zif_Async_graceful_shutdown, arginfo_Async_graceful_shutdown, 0, NULL, NULL)
	ZEND_FE_END
};

static const zend_function_entry class_Async_Completable_methods[] = {
	ZEND_RAW_FENTRY("cancel", NULL, arginfo_class_Async_Completable_cancel, ZEND_ACC_PUBLIC|ZEND_ACC_ABSTRACT, NULL, NULL)
	ZEND_RAW_FENTRY("isCompleted", NULL, arginfo_class_Async_Completable_isCompleted, ZEND_ACC_PUBLIC|ZEND_ACC_ABSTRACT, NULL, NULL)
	ZEND_RAW_FENTRY("isCancelled", NULL, arginfo_class_Async_Completable_isCancelled, ZEND_ACC_PUBLIC|ZEND_ACC_ABSTRACT, NULL, NULL)
	ZEND_FE_END
};

static zend_class_entry *register_class_Async_Awaitable(void)
{
	zend_class_entry ce, *class_entry;

	INIT_NS_CLASS_ENTRY(ce, "Async", "Awaitable", NULL);
	class_entry = zend_register_internal_interface(&ce);

	return class_entry;
}

static zend_class_entry *register_class_Async_Completable(zend_class_entry *class_entry_Async_Awaitable)
{
	zend_class_entry ce, *class_entry;

	INIT_NS_CLASS_ENTRY(ce, "Async", "Completable", class_Async_Completable_methods);
	class_entry = zend_register_internal_interface(&ce);
	zend_class_implements(class_entry, 1, class_entry_Async_Awaitable);

	return class_entry;
}

static zend_class_entry *register_class_Async_Signal(void)
{
	zend_class_entry *class_entry = zend_register_internal_enum("Async\\Signal", IS_LONG, NULL);

	zval enum_case_SIGHUP_value;
	ZVAL_LONG(&enum_case_SIGHUP_value, 1);
	zend_enum_add_case_cstr(class_entry, "SIGHUP", &enum_case_SIGHUP_value);

	zval enum_case_SIGINT_value;
	ZVAL_LONG(&enum_case_SIGINT_value, 2);
	zend_enum_add_case_cstr(class_entry, "SIGINT", &enum_case_SIGINT_value);

	zval enum_case_SIGQUIT_value;
	ZVAL_LONG(&enum_case_SIGQUIT_value, 3);
	zend_enum_add_case_cstr(class_entry, "SIGQUIT", &enum_case_SIGQUIT_value);

	zval enum_case_SIGILL_value;
	ZVAL_LONG(&enum_case_SIGILL_value, 4);
	zend_enum_add_case_cstr(class_entry, "SIGILL", &enum_case_SIGILL_value);

	zval enum_case_SIGABRT_value;
	ZVAL_LONG(&enum_case_SIGABRT_value, 6);
	zend_enum_add_case_cstr(class_entry, "SIGABRT", &enum_case_SIGABRT_value);

	zval enum_case_SIGFPE_value;
	ZVAL_LONG(&enum_case_SIGFPE_value, 8);
	zend_enum_add_case_cstr(class_entry, "SIGFPE", &enum_case_SIGFPE_value);

	zval enum_case_SIGKILL_value;
	ZVAL_LONG(&enum_case_SIGKILL_value, 9);
	zend_enum_add_case_cstr(class_entry, "SIGKILL", &enum_case_SIGKILL_value);

	zval enum_case_SIGUSR1_value;
	ZVAL_LONG(&enum_case_SIGUSR1_value, 10);
	zend_enum_add_case_cstr(class_entry, "SIGUSR1", &enum_case_SIGUSR1_value);

	zval enum_case_SIGSEGV_value;
	ZVAL_LONG(&enum_case_SIGSEGV_value, 11);
	zend_enum_add_case_cstr(class_entry, "SIGSEGV", &enum_case_SIGSEGV_value);

	zval enum_case_SIGUSR2_value;
	ZVAL_LONG(&enum_case_SIGUSR2_value, 12);
	zend_enum_add_case_cstr(class_entry, "SIGUSR2", &enum_case_SIGUSR2_value);

	zval enum_case_SIGTERM_value;
	ZVAL_LONG(&enum_case_SIGTERM_value, 15);
	zend_enum_add_case_cstr(class_entry, "SIGTERM", &enum_case_SIGTERM_value);

	zval enum_case_SIGBREAK_value;
	ZVAL_LONG(&enum_case_SIGBREAK_value, 21);
	zend_enum_add_case_cstr(class_entry, "SIGBREAK", &enum_case_SIGBREAK_value);

	zval enum_case_SIGABRT2_value;
	ZVAL_LONG(&enum_case_SIGABRT2_value, 22);
	zend_enum_add_case_cstr(class_entry, "SIGABRT2", &enum_case_SIGABRT2_value);

	zval enum_case_SIGWINCH_value;
	ZVAL_LONG(&enum_case_SIGWINCH_value, 28);
	zend_enum_add_case_cstr(class_entry, "SIGWINCH", &enum_case_SIGWINCH_value);

	return class_entry;
}
