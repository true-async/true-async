--TEST--
TaskGroup: a spawner cancelled after the queue's room woke it wakes the next parked spawner into that room
--FILE--
<?php

use Async\Future;
use Async\FutureState;
use Async\TaskGroup;
use function Async\await;
use function Async\spawn;
use function Async\suspend;

spawn(function () {
    $running_gate = new FutureState();
    $queued_gate = new FutureState();
    $group = new TaskGroup(concurrency: 1, queueLimit: 1);
    $group->spawn(fn() => (new Future($running_gate))->await());
    $group->spawn(fn() => (new Future($queued_gate))->await());
    $spawner = function (string $name) use ($group) {
        try {
            $group->spawn(fn() => $name);
            echo "$name accepted\n";
        } catch (Async\AsyncCancellation $cancellation) {
            echo "$name cancelled\n";
        }
    };
    $first_spawner = spawn($spawner, "S1");
    $second_spawner = spawn($spawner, "S2");

    suspend();
    $running_gate->complete(null);
    // The running task ends: the queued one starts and wakes S1, which is cancelled before it runs.
    suspend();
    $first_spawner->cancel();
    await($first_spawner);
    await($second_spawner);

    $queued_gate->complete(null);
    $group->close();
    $group->awaitCompletion();
    echo "count: ", count($group), "\n";
});
?>
--EXPECT--
S1 cancelled
S2 accepted
count: 3
