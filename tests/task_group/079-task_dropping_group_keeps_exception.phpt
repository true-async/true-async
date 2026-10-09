--TEST--
TaskGroup: a task whose closure releases the group's last reference after throwing keeps its own exception
--FILE--
<?php

use Async\Future;
use Async\FutureState;
use Async\TaskGroup;

$closed = new FutureState();
$group = new TaskGroup();
$group->finally(function (TaskGroup $group) use ($closed) {
    $error = $group->getErrors()[0];
    echo get_class($error), ": ", $error->getMessage(), "\n";
    var_dump($error->getPrevious());
    $closed->complete(null);
});
$group->spawn(function () use ($group) {
    throw new RuntimeException("own");
});
unset($group);

(new Future($closed))->await();
?>
--EXPECT--
RuntimeException: own
NULL
