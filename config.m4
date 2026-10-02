PHP_ARG_ENABLE([true-async],
  [whether to enable the true_async extension],
  [AS_HELP_STRING([--enable-true-async],
    [Enable the true_async extension: coroutines on the PHP scheduler API])],
  [no])

PHP_ARG_ENABLE([true-async-known-answer],
  [whether to plant the mutation known-answer functions],
  [AS_HELP_STRING([--enable-true-async-known-answer],
    [Plant two functions for the mutation tool's known-answer check (the mull lane only)])],
  [no],
  [no])

if test "$PHP_TRUE_ASYNC" != "no"; then
  dnl The scheduler API exists only in a core built from async-core-io; a stock php-src fails
  dnl here instead of at the first compile error.
  old_CPPFLAGS=$CPPFLAGS
  CPPFLAGS="$CPPFLAGS $INCLUDES"
  AC_CHECK_HEADER([Zend/zend_async_API.h], [],
    [AC_MSG_ERROR([true_async needs a PHP core with the scheduler API (Zend/zend_async_API.h)])],
    [#include "php.h"])
  CPPFLAGS=$old_CPPFLAGS

  true_async_sources="src/true_async.c"

  if test "$PHP_TRUE_ASYNC_KNOWN_ANSWER" != "no"; then
    AC_DEFINE([TRUE_ASYNC_KNOWN_ANSWER], [1], [Define to 1 to plant the known-answer functions.])
    true_async_sources="$true_async_sources src/known_answer.c"
  fi

  PHP_NEW_EXTENSION([true_async], [$true_async_sources],
    [$ext_shared],,
    [-DZEND_ENABLE_STATIC_TSRMLS_CACHE=1])
  PHP_ADD_BUILD_DIR([$ext_builddir/src])
fi
