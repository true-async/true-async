/* This is a generated file, edit true_async.stub.php instead.
 * Stub hash: f8aca8104b2bfcc498fd63eaa47bb362b15ee1a3 */

ZEND_BEGIN_ARG_WITH_RETURN_OBJ_INFO_EX(arginfo_Async_spawn, 0, 1, Async\\Coroutine, 0)
	ZEND_ARG_TYPE_INFO(0, task, IS_CALLABLE, 0)
	ZEND_ARG_VARIADIC_TYPE_INFO(0, args, IS_MIXED, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_OBJ_INFO_EX(arginfo_Async_current_coroutine, 0, 0, Async\\Coroutine, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_Async_get_coroutines, 0, 0, IS_ARRAY, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_class_Async_Completable_cancel, 0, 0, IS_VOID, 0)
	ZEND_ARG_OBJ_INFO_WITH_DEFAULT_VALUE(0, cancellation, Async\\AsyncCancellation, 1, "null")
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_class_Async_Completable_isCompleted, 0, 0, _IS_BOOL, 0)
ZEND_END_ARG_INFO()

#define arginfo_class_Async_Completable_isCancelled arginfo_class_Async_Completable_isCompleted

ZEND_FUNCTION(Async_spawn);
ZEND_FUNCTION(Async_current_coroutine);
ZEND_FUNCTION(Async_get_coroutines);

static const zend_function_entry ext_functions[] = {
	ZEND_RAW_FENTRY(ZEND_NS_NAME("Async", "spawn"), zif_Async_spawn, arginfo_Async_spawn, 0, NULL, NULL)
	ZEND_RAW_FENTRY(ZEND_NS_NAME("Async", "current_coroutine"), zif_Async_current_coroutine, arginfo_Async_current_coroutine, 0, NULL, NULL)
	ZEND_RAW_FENTRY(ZEND_NS_NAME("Async", "get_coroutines"), zif_Async_get_coroutines, arginfo_Async_get_coroutines, 0, NULL, NULL)
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
