--TEST--
TaskGroup: a newcomer's spawn() waits behind a spawner woken to take the queue's room, even with no other spawner parked
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
    $queued_gate = new FutureState();
    $group = new TaskGroup(concurrency: 1, queueLimit: 1);
    $group->spawn(fn() => (new Future($first_gate))->await());
    $group->spawn(fn() => (new Future($queued_gate))->await());
    $first_spawner = spawn(function () use ($group) {
        $group->spawn(fn() => "S1");
        echo "S1 accepted\n";
    });

    suspend();
    $first_gate->complete(null);
    // The first task's end starts the queued one and wakes S1, which runs after this coroutine parks below.
    suspend();
    spawn(function () use ($queued_gate, $first_spawner) {
        await($first_spawner);
        $queued_gate->complete(null);
    });
    $group->spawn(fn() => "M");
    echo "M accepted\n";

    $group->close();
    $group->awaitCompletion();
    echo "count: ", count($group), "\n";
});
?>
--EXPECT--
S1 accepted
M accepted
count: 4
