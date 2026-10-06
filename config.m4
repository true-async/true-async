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

PHP_ARG_ENABLE([true-async-test-hooks],
  [whether to build the test hooks],
  [AS_HELP_STRING([--enable-true-async-test-hooks],
    [Build TrueAsync\\Test functions that drive internal structures (tests/internal/)])],
  [no],
  [no])

PHP_ARG_ENABLE([true-async-fuzz],
  [whether to build the scheduler's fuzz hook],
  [AS_HELP_STRING([--enable-true-async-fuzz],
    [Let TRUE_ASYNC_SCHED=random:<seed> permute the run queue (tools/test.py --seeds)])],
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

  true_async_sources="src/true_async.c src/true_async_API.c src/coroutine.c src/exceptions.c src/scheduler.c src/reactor.c src/future.c src/await.c src/internal/circular_buffer.c"

  if test "$PHP_TRUE_ASYNC_KNOWN_ANSWER" != "no"; then
    AC_DEFINE([TRUE_ASYNC_KNOWN_ANSWER], [1], [Define to 1 to plant the known-answer functions.])
    true_async_sources="$true_async_sources src/known_answer.c"
  fi

  if test "$PHP_TRUE_ASYNC_TEST_HOOKS" != "no"; then
    AC_DEFINE([TRUE_ASYNC_TEST_HOOKS], [1], [Define to 1 to build the test hooks.])
    true_async_sources="$true_async_sources src/test_hooks.c"
    dnl trigger_fire() starts a thread; glibc before 2.34 keeps pthread_create in libpthread.
    PHP_ADD_LIBRARY([pthread],, [TRUE_ASYNC_SHARED_LIBADD])
    PHP_SUBST([TRUE_ASYNC_SHARED_LIBADD])
  fi

  if test "$PHP_TRUE_ASYNC_FUZZ" != "no"; then
    AC_DEFINE([TRUE_ASYNC_FUZZ], [1], [Define to 1 to build the scheduler's fuzz hook.])
    true_async_sources="$true_async_sources src/internal/fuzz.c"
  fi

  dnl An event helper applied to a coroutine must not compile (dev/plans/S3.md, section 3);
  dnl gcc 13 only warns about it without the flag. An undeclared function fails here too, not at
  dnl dlopen. Only get_module is exported (ZEND_DLEXPORT): phpize, unlike an in-tree build, does
  dnl not hide the rest, and php-async exports the same circular_buffer_* names.
  PHP_NEW_EXTENSION([true_async], [$true_async_sources],
    [$ext_shared],,
    [-DZEND_ENABLE_STATIC_TSRMLS_CACHE=1 -Werror=incompatible-pointer-types -Werror=implicit-function-declaration -fvisibility=hidden])
  PHP_ADD_BUILD_DIR([$ext_builddir/src])
  PHP_ADD_BUILD_DIR([$ext_builddir/src/internal])
fi
