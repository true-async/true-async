--TEST--
TaskGroup: a group whose last holder is a task's closure is closed when that task ends, cancelling the other task
--FILE--
<?php

use Async\Future;
use Async\FutureState;
use Async\TaskGroup;

$started = new FutureState();
$closed = new FutureState();
$group = new TaskGroup();
$group->spawn(function () use ($started) {
    $started->complete(null);

    try {
        (new Future(new FutureState()))->await();
    } catch (Async\AsyncCancellation $error) {
        echo "other task: ", $error->getMessage(), "\n";
    }
});
$group->finally(function () use ($closed) {
    echo "finally\n";
    $closed->complete(null);
});

(new Future($started))->await();
$group->spawn(function () use ($group) {
    echo "holder task ends\n";
});
unset($group);

(new Future($closed))->await();
?>
--EXPECT--
holder task ends
other task: Scope is being disposed due to TaskGroup destruction
finally
