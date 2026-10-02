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
#ifndef TRUE_ASYNC_TEST_HOOKS_H
#define TRUE_ASYNC_TEST_HOOKS_H

#include "php.h"

/* Registered at MINIT in a build with --enable-true-async-test-hooks. */
extern const zend_function_entry true_async_test_hooks_functions[];

#endif /* TRUE_ASYNC_TEST_HOOKS_H */
