--TEST--
TaskGroup: a foreach refused in scheduler context takes no error, which the dropped group still reports
--FILE--
<?php

use Async\TaskGroup;
use TrueAsync\Test;

$group = new TaskGroup();
$group->spawn(function () {
    throw new RuntimeException("boom");
});
$group->close();
$group->awaitCompletion();

Test\defer('A', null, function () use ($group) {
    try {
        foreach ($group as $key => $pair) {
            echo "yielded $key\n";
        }
    } catch (Error $error) {
        echo "foreach: ", $error->getMessage(), "\n";
    }
});

Async\suspend();
unset($group);
echo "after unset\n";
?>
--EXPECTF--
microtask A sched=1
foreach: The operation cannot be executed in the scheduler context
released A
after unset

Fatal error: Uncaught Async\CompositeException in %s:%d
Stack trace:
#0 {main}
  thrown in %s on line %d
