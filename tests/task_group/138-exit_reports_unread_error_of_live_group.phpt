--TEST--
TaskGroup: exit() with a completed group holding an error nobody read reports it with the exit's exceptions
--FILE--
<?php

use Async\TaskGroup;

$group = new TaskGroup();
$group->spawn(function () {
    throw new RuntimeException("nobody read me");
});
$group->close();
$group->awaitCompletion();

exit("exit\n");
?>
--EXPECTF--
exit

Fatal error: Uncaught Async\CompositeException in %s:%d
Stack trace:
#0 {main}
  thrown in %s on line %d
