--TEST--
TaskGroup: foreach yields the tasks in the order they end, not the order they were spawned
--FILE--
<?php

use Async\Future;
use Async\FutureState;
use Async\TaskGroup;
use function Async\spawn;

spawn(function () {
    $gate = new FutureState();
    $group = new TaskGroup();
    $group->spawnWithKey("first spawned", function () use ($gate) {
        (new Future($gate))->await();

        return 1;
    });
    $group->spawnWithKey("first to end", function () use ($gate) {
        $gate->complete(null);

        return 2;
    });
    $group->close();

    foreach ($group as $key => [$result, $error]) {
        echo "$key => $result\n";
    }
});
?>
--EXPECT--
first to end => 2
first spawned => 1
