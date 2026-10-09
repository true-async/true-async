--TEST--
TaskGroup: race() on a completed group settles with the first task in spawn order, not the first to end
--FILE--
<?php

use Async\Future;
use Async\FutureState;
use Async\TaskGroup;
use function Async\spawn;

spawn(function () {
    $gate = new FutureState();
    $group = new TaskGroup();
    $group->spawn(function () use ($gate) {
        (new Future($gate))->await();

        return "first spawned";
    });
    $group->spawn(function () use ($gate) {
        $gate->complete(null);

        return "first to end";
    });
    $group->close();
    $group->awaitCompletion();

    echo $group->race()->await(), "\n";
});
?>
--EXPECT--
first spawned
