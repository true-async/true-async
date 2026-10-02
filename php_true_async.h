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
#ifndef PHP_TRUE_ASYNC_H
#define PHP_TRUE_ASYNC_H

/* Kept at the repository root: a static build of php-src finds the module entry by grepping the
 * *.h files of ext/<name> for "phpext_", without descending into subdirectories. */

extern zend_module_entry true_async_module_entry;
#define phpext_true_async_ptr &true_async_module_entry

#define PHP_TRUE_ASYNC_VERSION "0.1.0-dev"

#endif /* PHP_TRUE_ASYNC_H */
