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

#ifdef TRUE_ASYNC_KNOWN_ANSWER
#include "known_answer.h"
#define TRUE_ASYNC_FUNCTIONS true_async_known_answer_functions
#else
#define TRUE_ASYNC_FUNCTIONS NULL
#endif

/* Off by default, as test_scheduler.enable: the scheduler slots are process-wide, and a binary
 * that loads the extension must still be able to run another provider. */
PHP_INI_BEGIN()
	PHP_INI_ENTRY("true_async.enable", "0", PHP_INI_SYSTEM, NULL)
PHP_INI_END()

static PHP_MINIT_FUNCTION(true_async)
{
	REGISTER_INI_ENTRIES();

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
	STANDARD_MODULE_PROPERTIES
};
/* clang-format on */

#ifdef COMPILE_DL_TRUE_ASYNC
#ifdef ZTS
ZEND_TSRMLS_CACHE_DEFINE()
#endif
ZEND_GET_MODULE(true_async)
#endif
