/* This is a generated file, edit timeout.stub.php instead.
 * Stub hash: 12713a32925f7137c8442ca15fba8707a77a3917 */

ZEND_BEGIN_ARG_INFO_EX(arginfo_class_Async_Timeout___construct, 0, 0, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_class_Async_Timeout_cancel, 0, 0, IS_VOID, 0)
	ZEND_ARG_OBJ_INFO_WITH_DEFAULT_VALUE(0, cancellation, Async\\AsyncCancellation, 1, "null")
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_class_Async_Timeout_isCompleted, 0, 0, _IS_BOOL, 0)
ZEND_END_ARG_INFO()

#define arginfo_class_Async_Timeout_isCancelled arginfo_class_Async_Timeout_isCompleted

ZEND_METHOD(Async_Timeout, __construct);
ZEND_METHOD(Async_Timeout, cancel);
ZEND_METHOD(Async_Timeout, isCompleted);
ZEND_METHOD(Async_Timeout, isCancelled);

static const zend_function_entry class_Async_Timeout_methods[] = {
	ZEND_ME(Async_Timeout, __construct, arginfo_class_Async_Timeout___construct, ZEND_ACC_PRIVATE)
	ZEND_ME(Async_Timeout, cancel, arginfo_class_Async_Timeout_cancel, ZEND_ACC_PUBLIC)
	ZEND_ME(Async_Timeout, isCompleted, arginfo_class_Async_Timeout_isCompleted, ZEND_ACC_PUBLIC)
	ZEND_ME(Async_Timeout, isCancelled, arginfo_class_Async_Timeout_isCancelled, ZEND_ACC_PUBLIC)
	ZEND_FE_END
};

static zend_class_entry *register_class_Async_Timeout(zend_class_entry *class_entry_Async_Completable)
{
	zend_class_entry ce, *class_entry;

	INIT_NS_CLASS_ENTRY(ce, "Async", "Timeout", class_Async_Timeout_methods);
	class_entry = zend_register_internal_class_with_flags(&ce, NULL, ZEND_ACC_FINAL|ZEND_ACC_NO_DYNAMIC_PROPERTIES|ZEND_ACC_NOT_SERIALIZABLE);
	zend_class_implements(class_entry, 1, class_entry_Async_Completable);

	return class_entry;
}
