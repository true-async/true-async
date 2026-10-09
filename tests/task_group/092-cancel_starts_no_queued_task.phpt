--TEST--
TaskGroup: cancel() starts no queued task, which ends with the cancellation
--FILE--
<?php

use Async\Future;
use Async\FutureState;
use Async\TaskGroup;

$started = new FutureState();
$group = new TaskGroup(concurrency: 1);
$group->spawn(function () use ($started) {
    $started->complete(null);
    (new Future(new FutureState()))->await();
});
$group->spawn(function () {
    echo "queued task ran\n";
});

(new Future($started))->await();
$group->cancel();
$group->all(ignoreErrors: true)->await();

echo get_class($group->getErrors()[1]), ": ", $group->getErrors()[1]->getMessage(), "\n";
?>
--EXPECT--
Async\AsyncCancellation: TaskGroup cancelled
