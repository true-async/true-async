--TEST--
TaskGroup: a group dropped by the main code with an error nobody saw and no handler ends the request with Uncaught CompositeException
--FILE--
<?php

use Async\TaskGroup;

$group = new TaskGroup();
$group->spawn(function () {
    throw new RuntimeException("boom");
});
$group->close();
$group->awaitCompletion();
echo "before unset\n";
unset($group);
echo "after unset\n";
?>
--EXPECTF--
before unset
after unset

Fatal error: Uncaught Async\CompositeException in %s104-unseen_error_uncaught_in_main.php:12
Stack trace:
#0 {main}
  thrown in %s104-unseen_error_uncaught_in_main.php on line 12
