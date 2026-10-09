--TEST--
TaskGroup: exit() reports the error nobody read while user code holds the reporter of the dropped group
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

$coroutines = Async\get_coroutines();

exit("exit\n");
?>
--EXPECTF--
exit

Fatal error: Uncaught Async\CompositeException in %s:%d
Stack trace:
#0 {main}
  thrown in %s on line %d
