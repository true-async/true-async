--TEST--
TaskGroup: close() wakes every spawner parked on a full queue, and each throws
--FILE--
<?php

use Async\Future;
use Async\FutureState;
use Async\TaskGroup;
use function Async\await;
use function Async\spawn;
use function Async\suspend;

spawn(function () {
    $gate = new FutureState();
    $group = new TaskGroup(concurrency: 1, queueLimit: 1);
    $group->spawn(fn() => (new Future($gate))->await());
    $group->spawn(fn() => "queued");
    $spawner = function (string $name) use ($group) {
        try {
            $group->spawn(fn() => $name);
            echo "$name accepted\n";
        } catch (Async\AsyncException $exception) {
            echo "$name: ", $exception->getMessage(), "\n";
        }
    };
    $first_spawner = spawn($spawner, "S1");
    $second_spawner = spawn($spawner, "S2");

    suspend();
    $group->close();
    await($first_spawner);
    await($second_spawner);
    $gate->complete(null);
    $group->awaitCompletion();
    echo "count: ", count($group), "\n";
});
?>
--EXPECT--
S1: Cannot spawn tasks on a closed TaskGroup
S2: Cannot spawn tasks on a closed TaskGroup
count: 2
