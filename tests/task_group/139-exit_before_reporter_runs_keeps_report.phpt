--TEST--
TaskGroup: exit() before the reporter of a dropped group has run keeps the report of the error nobody read
--FILE--
<?php

use Async\TaskGroup;

$group = new TaskGroup();
$group->spawn(function () {
    throw new RuntimeException("nobody read me");
});
$group->close();
$group->awaitCompletion();
unset($group);

exit("exit\n");
?>
--EXPECTF--
exit

Fatal error: Uncaught Async\CompositeException in %s:%d
Stack trace:
#0 {main}
  thrown in %s on line %d
