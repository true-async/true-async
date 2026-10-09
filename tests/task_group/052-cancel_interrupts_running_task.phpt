--TEST--
TaskGroup: cancel() of a group under the global scope interrupts its running task
--FILE--
<?php

use Async\Future;
use Async\FutureState;
use Async\TaskGroup;

$started = new FutureState();
$group = new TaskGroup();
$group->spawn(function () use ($started) {
    $started->complete(null);

    try {
        (new Future(new FutureState()))->await();
    } catch (Async\AsyncCancellation $error) {
        echo "running task: ", $error->getMessage(), "\n";
        throw $error;
    }
});

(new Future($started))->await();
$group->cancel();
$group->all(ignoreErrors: true)->await();

echo get_class($group->getErrors()[0]), "\n";
?>
--EXPECT--
running task: TaskGroup cancelled
Async\AsyncCancellation
