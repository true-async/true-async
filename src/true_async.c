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
#include "php_true_async.h"
#include "coroutine.h"
#include "exceptions.h"
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

zend_class_entry *async_ce_awaitable = NULL;
zend_class_entry *async_ce_completable = NULL;

/* Only this extension's classes implement Awaitable: generic wait code reads an awaitable's memory
 * as a coroutine or an event (dev/plans/S3.md, section 13, bug 10). Completable extends it, so
 * this covers both. */
static int awaitable_gets_implemented(zend_class_entry *interface, zend_class_entry *class_entry)
{
	if (class_entry->type == ZEND_INTERNAL_CLASS && class_entry->info.internal.module == &true_async_module_entry) {
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

#ifdef TRUE_ASYNC_TEST_HOOKS
	/* A second table beside TRUE_ASYNC_FUNCTIONS: the mull lane builds the known-answer functions
	 * and the hooks together. */
	if (zend_register_functions(NULL, true_async_test_hooks_functions, NULL, type) == FAILURE) {
		return FAILURE;
	}
#endif

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

/* clang-format off */
zend_module_entry true_async_module_entry = {
	STANDARD_MODULE_HEADER,
	"true_async",
	TRUE_ASYNC_FUNCTIONS,
	PHP_MINIT(true_async),
	PHP_MSHUTDOWN(true_async),
	NULL,
	NULL,
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
