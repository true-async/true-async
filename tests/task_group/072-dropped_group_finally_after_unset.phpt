--TEST--
TaskGroup: a group dropped unclosed runs its finally handlers after the code that follows unset()
--FILE--
<?php

use Async\Future;
use Async\FutureState;
use Async\TaskGroup;

$ran = new FutureState();
$group = new TaskGroup();
$group->spawn(fn() => "done");
$group->all()->await();
$group->finally(function (TaskGroup $group) use ($ran) {
    echo "finally: ", count($group), " task\n";
    $ran->complete(null);
});

unset($group);
echo "after unset\n";
(new Future($ran))->await();
?>
--EXPECT--
after unset
finally: 1 task
