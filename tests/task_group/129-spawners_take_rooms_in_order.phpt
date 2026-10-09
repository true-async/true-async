--TEST--
TaskGroup: a spawner leaving the park wakes no other for a room already promised to a woken spawner, so the spawners go in their order
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
    $first_queued_gate = new FutureState();
    $second_queued_gate = new FutureState();
    $group = new TaskGroup(concurrency: 2, queueLimit: 2);
    $group->spawn(fn() => (new Future($first_gate))->await());
    $group->spawn(fn() => (new Future($second_gate))->await());
    $group->spawn(fn() => (new Future($first_queued_gate))->await());
    $group->spawn(fn() => (new Future($second_queued_gate))->await());
    $spawner = function (string $name) use ($group) {
        $group->spawn(fn() => $name);
        echo "$name accepted\n";
    };
    $spawners = [];

    foreach (["S0", "S1", "S2", "S3"] as $name) {
        $spawners[] = spawn($spawner, $name);
    }

    suspend();
    // Each end starts a queued task and wakes S0, then S1, each for its own queue place; S0's leaving wakes
    // nobody for S1's place.
    $first_gate->complete(null);
    $second_gate->complete(null);
    await($spawners[0]);
    await($spawners[1]);

    $first_queued_gate->complete(null);
    $second_queued_gate->complete(null);

    foreach ($spawners as $coroutine) {
        await($coroutine);
    }

    $group->close();
    $group->awaitCompletion();
    echo "count: ", count($group), "\n";
});
?>
--EXPECT--
S0 accepted
S1 accepted
S2 accepted
S3 accepted
count: 8
