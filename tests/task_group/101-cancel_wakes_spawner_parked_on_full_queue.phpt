--TEST--
TaskGroup: cancel() wakes a spawn() parked on a full queue, which throws the closed message, and the queued task never runs
--FILE--
<?php

use Async\Future;
use Async\FutureState;
use Async\TaskGroup;
use function Async\await;
use function Async\spawn;
use function Async\suspend;

spawn(function () {
    $group = new TaskGroup(concurrency: 1, queueLimit: 1);
    $group->spawn(function () {
        (new Future(new FutureState()))->await();
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
    $group->cancel();
    await($spawner);
    echo "count: ", count($group), "\n";
});
?>
--EXPECT--
spawner: Cannot spawn tasks on a closed TaskGroup
count: 2
