/* This is a generated file, edit true_async.stub.php instead.
 * Stub hash: 863ac12e04015c75ef337a4d8871d772a35aa8ad */

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_class_Async_Completable_cancel, 0, 0, IS_VOID, 0)
	ZEND_ARG_OBJ_INFO_WITH_DEFAULT_VALUE(0, cancellation, Async\\AsyncCancellation, 1, "null")
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_class_Async_Completable_isCompleted, 0, 0, _IS_BOOL, 0)
ZEND_END_ARG_INFO()

#define arginfo_class_Async_Completable_isCancelled arginfo_class_Async_Completable_isCompleted


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
