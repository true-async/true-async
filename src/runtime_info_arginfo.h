/* This is a generated file, edit runtime_info.stub.php instead.
 * Stub hash: c9a68adb2e6b8010750854d76c3ba33b18f764c8 */

ZEND_BEGIN_ARG_INFO_EX(arginfo_class_Async_CpuSnapshot___construct, 0, 0, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_OBJ_INFO_EX(arginfo_class_Async_CpuSnapshot_now, 0, 0, Async\\CpuSnapshot, 0)
ZEND_END_ARG_INFO()

ZEND_METHOD(Async_CpuSnapshot, __construct);
ZEND_METHOD(Async_CpuSnapshot, now);

static const zend_function_entry class_Async_CpuSnapshot_methods[] = {
	ZEND_ME(Async_CpuSnapshot, __construct, arginfo_class_Async_CpuSnapshot___construct, ZEND_ACC_PRIVATE)
	ZEND_ME(Async_CpuSnapshot, now, arginfo_class_Async_CpuSnapshot_now, ZEND_ACC_PUBLIC|ZEND_ACC_STATIC)
	ZEND_FE_END
};

static zend_class_entry *register_class_Async_CpuSnapshot(void)
{
	zend_class_entry ce, *class_entry;

	INIT_NS_CLASS_ENTRY(ce, "Async", "CpuSnapshot", class_Async_CpuSnapshot_methods);
	class_entry = zend_register_internal_class_with_flags(&ce, NULL, ZEND_ACC_FINAL|ZEND_ACC_NO_DYNAMIC_PROPERTIES|ZEND_ACC_NOT_SERIALIZABLE);

	zval property_wallNs_default_value;
	ZVAL_UNDEF(&property_wallNs_default_value);
	zend_string *property_wallNs_name = zend_string_init("wallNs", sizeof("wallNs") - 1, true);
	zend_declare_typed_property(class_entry, property_wallNs_name, &property_wallNs_default_value, ZEND_ACC_PUBLIC|ZEND_ACC_READONLY, NULL, (zend_type) ZEND_TYPE_INIT_MASK(MAY_BE_LONG));
	zend_string_release_ex(property_wallNs_name, true);

	zval property_processUserNs_default_value;
	ZVAL_UNDEF(&property_processUserNs_default_value);
	zend_string *property_processUserNs_name = zend_string_init("processUserNs", sizeof("processUserNs") - 1, true);
	zend_declare_typed_property(class_entry, property_processUserNs_name, &property_processUserNs_default_value, ZEND_ACC_PUBLIC|ZEND_ACC_READONLY, NULL, (zend_type) ZEND_TYPE_INIT_MASK(MAY_BE_LONG));
	zend_string_release_ex(property_processUserNs_name, true);

	zval property_processSystemNs_default_value;
	ZVAL_UNDEF(&property_processSystemNs_default_value);
	zend_string *property_processSystemNs_name = zend_string_init("processSystemNs", sizeof("processSystemNs") - 1, true);
	zend_declare_typed_property(class_entry, property_processSystemNs_name, &property_processSystemNs_default_value, ZEND_ACC_PUBLIC|ZEND_ACC_READONLY, NULL, (zend_type) ZEND_TYPE_INIT_MASK(MAY_BE_LONG));
	zend_string_release_ex(property_processSystemNs_name, true);

	zval property_systemIdleNs_default_value;
	ZVAL_UNDEF(&property_systemIdleNs_default_value);
	zend_string *property_systemIdleNs_name = zend_string_init("systemIdleNs", sizeof("systemIdleNs") - 1, true);
	zend_declare_typed_property(class_entry, property_systemIdleNs_name, &property_systemIdleNs_default_value, ZEND_ACC_PUBLIC|ZEND_ACC_READONLY, NULL, (zend_type) ZEND_TYPE_INIT_MASK(MAY_BE_LONG));
	zend_string_release_ex(property_systemIdleNs_name, true);

	zval property_systemBusyNs_default_value;
	ZVAL_UNDEF(&property_systemBusyNs_default_value);
	zend_string *property_systemBusyNs_name = zend_string_init("systemBusyNs", sizeof("systemBusyNs") - 1, true);
	zend_declare_typed_property(class_entry, property_systemBusyNs_name, &property_systemBusyNs_default_value, ZEND_ACC_PUBLIC|ZEND_ACC_READONLY, NULL, (zend_type) ZEND_TYPE_INIT_MASK(MAY_BE_LONG));
	zend_string_release_ex(property_systemBusyNs_name, true);

	zval property_cpuCount_default_value;
	ZVAL_UNDEF(&property_cpuCount_default_value);
	zend_string *property_cpuCount_name = zend_string_init("cpuCount", sizeof("cpuCount") - 1, true);
	zend_declare_typed_property(class_entry, property_cpuCount_name, &property_cpuCount_default_value, ZEND_ACC_PUBLIC|ZEND_ACC_READONLY, NULL, (zend_type) ZEND_TYPE_INIT_MASK(MAY_BE_LONG));
	zend_string_release_ex(property_cpuCount_name, true);

	return class_entry;
}
