--TEST--
TaskGroup: the collector does not report a coroutine awaiting all() while the group's task sleeps
--INI--
true_async.partial_deadlock=cancel
true_async.partial_deadlock_interval=0
--FILE--
<?php

use Async\TaskGroup;
use function Async\await;
use function Async\delay;
use function Async\spawn;

$awaiter = spawn(function () {
    $group = new TaskGroup();
    $group->spawn(function () {
        delay(20);

        return "slept";
    });

    return $group->all()->await();
});

var_dump(await($awaiter));
?>
--EXPECT--
array(1) {
  [0]=>
  string(5) "slept"
}
