--TEST--
TaskGroup: finally handlers wait for the completion: an open group that settles calls none, the closed one calls it once
--FILE--
<?php

use Async\Future;
use Async\FutureState;
use Async\TaskGroup;
use function Async\spawn;
use function Async\suspend;

spawn(function () {
    $called = new FutureState();
    $calls = 0;
    $group = new TaskGroup();
    $group->finally(function () use (&$calls, $called) {
        $calls++;
        $called->complete(null);
    });
    $group->spawn(fn() => 1);

    suspend();
    suspend();
    echo "settled while open: ", var_export($group->isFinished(), true), ", calls: $calls\n";

    $group->spawn(fn() => 2);
    $group->close();
    $group->awaitCompletion();
    (new Future($called))->await();
    suspend();
    echo "completed, calls: $calls\n";
});
?>
--EXPECT--
settled while open: true, calls: 0
completed, calls: 1
