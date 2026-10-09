--TEST--
TaskSet: the first joinNext() delivers the task that ended first, not the one spawned first
--FILE--
<?php

use Async\Future;
use Async\FutureState;
use Async\TaskSet;

$secondEnded = new FutureState();
$set = new TaskSet();
$set->spawn(function () use ($secondEnded) {
    (new Future($secondEnded))->await();
    return "spawned first";
});
$set->spawn(function () use ($secondEnded) {
    $secondEnded->complete(null);
    return "spawned second";
});

var_dump($set->joinNext()->await());
?>
--EXPECT--
string(14) "spawned second"
