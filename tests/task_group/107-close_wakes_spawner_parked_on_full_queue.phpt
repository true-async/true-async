--TEST--
TaskGroup: close() wakes a spawn() parked on a full queue, which throws the closed message, while the queued task still runs
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
    $group->spawn(function () use ($gate) {
        (new Future($gate))->await();
    });
    $group->spawn(function () {
        echo "queued task ran\n";
    });
    $spawner = spawn(function () use ($group) {
        try {
            $group->spawn(fn() => "third");
            echo "third accepted\n";
        } catch (Async\AsyncException $exception) {
            echo "spawner: ", $exception->getMessage(), "\n";
        }
    });

    suspend();
    $group->close();
    await($spawner);
    $gate->complete(null);
    $group->awaitCompletion();
    echo "count: ", count($group), "\n";
});
?>
--EXPECT--
spawner: Cannot spawn tasks on a closed TaskGroup
queued task ran
count: 2
