--TEST--
TaskGroup: a group dropped in a coroutine with an error nobody saw and no handler ends the request with Uncaught CompositeException
--FILE--
<?php

use Async\TaskGroup;
use function Async\spawn;

spawn(function () {
    $group = new TaskGroup();
    $group->spawn(function () {
        throw new RuntimeException("boom");
    });
    $group->close();
    $group->awaitCompletion();
    echo "before unset\n";
    unset($group);
    echo "after unset\n";
});
?>
--EXPECTF--
before unset
after unset

Fatal error: Uncaught Async\CompositeException in %s103-unseen_error_uncaught_in_coroutine.php:14
Stack trace:
#0 [internal function]: {closure:%s103-unseen_error_uncaught_in_coroutine.php:6}()
#1 {main}
  thrown in %s103-unseen_error_uncaught_in_coroutine.php on line 14
