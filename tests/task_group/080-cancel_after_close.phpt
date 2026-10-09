--TEST--
TaskGroup: cancel() after close() interrupts a running task
--FILE--
<?php

use Async\Future;
use Async\FutureState;
use Async\TaskGroup;

$started = new FutureState();
$group = new TaskGroup();
$group->spawn(function () use ($started) {
    $started->complete(null);
    (new Future(new FutureState()))->await();
});

(new Future($started))->await();
$group->close();
$group->cancel();
$group->all(ignoreErrors: true)->await();

echo get_class($group->getErrors()[0]), "\n";
?>
--EXPECT--
Async\AsyncCancellation
