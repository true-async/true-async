--TEST--
TaskGroup: a woken spawner that starts its task with a slot freed meanwhile wakes the next parked spawner into the queue's room
--FILE--
<?php

use Async\Future;
use Async\FutureState;
use Async\TaskGroup;
use function Async\await;
use function Async\spawn;
use function Async\suspend;

spawn(function () {
    $first_gate = new FutureState();
    $second_gate = new FutureState();
    $queued_gate = new FutureState();
    $group = new TaskGroup(concurrency: 2, queueLimit: 1);
    $group->spawn(fn() => (new Future($first_gate))->await());
    $group->spawn(fn() => (new Future($second_gate))->await());
    $group->spawn(fn() => (new Future($queued_gate))->await());
    $spawner = function (string $name) use ($group) {
        $group->spawn(fn() => $name);
        echo "$name accepted\n";
    };
    $first_spawner = spawn($spawner, "S1");
    $second_spawner = spawn($spawner, "S2");

    suspend();
    // The first task's end starts the queued one and wakes S1; the second's frees a slot before S1 runs.
    $first_gate->complete(null);
    $second_gate->complete(null);
    await($first_spawner);
    await($second_spawner);

    $queued_gate->complete(null);
    $group->close();
    $group->awaitCompletion();
    echo "count: ", count($group), "\n";
});
?>
--EXPECT--
S1 accepted
S2 accepted
count: 5
