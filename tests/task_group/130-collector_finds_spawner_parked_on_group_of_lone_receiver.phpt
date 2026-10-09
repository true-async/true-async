--TEST--
TaskGroup: the collector finds a spawn() parked on a full queue of a group whose only task waits on a channel nobody else holds
--INI--
true_async.partial_deadlock=cancel
true_async.partial_deadlock_interval=0
--FILE--
<?php

use Async\Channel;
use Async\TaskGroup;
use function Async\delay;
use function Async\spawn;

spawn(function () {
    $group = new TaskGroup(concurrency: 1, queueLimit: 1);
    $group->spawn(function () {
        (new Channel(0))->recv();
    });
    $group->spawn(fn() => "queued");

    try {
        $group->spawn(fn() => "third");
    } catch (Async\AsyncCancellation $cancellation) {
        echo "spawner: ", $cancellation->getMessage(), "\n";
    }

    $group->close();

    foreach ($group as $key => [$result, $error]) {
        echo "task $key: ", $error !== null ? $error->getMessage() : $result, "\n";
    }
});

// The sleep keeps the scheduler from a full deadlock, so the collector runs at its idle point.
delay(100);
echo "end\n";
?>
--EXPECTF--
Warning: Partial deadlock: coroutine #%d spawned at %s:%d can never wake (TaskGroup(total=2, active=1, queued=1): spawn() on a full queue) in %s on line %d

Warning: Partial deadlock: coroutine #%d spawned at %s:%d can never wake (Channel(capacity=0, receivers=1, senders=0, reserved receivers=0, reserved senders=0)) in %s on line %d
spawner: Deadlock detected
task 0: Deadlock detected
task 1: queued
end
