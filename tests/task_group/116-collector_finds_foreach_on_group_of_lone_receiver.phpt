--TEST--
TaskGroup: the collector finds a foreach parked on a group whose only task waits on a channel nobody else holds
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
    $group = new TaskGroup();
    $group->spawn(function () {
        (new Channel(0))->recv();
    });
    $group->close();

    try {
        foreach ($group as $key => $pair) {
            echo "yielded $key\n";
        }
    } catch (Async\AsyncCancellation $cancellation) {
        echo "consumer: ", $cancellation->getMessage(), "\n";
    }

    foreach ($group as $key => [$result, $error]) {
        echo "task $key: ", $error->getMessage(), "\n";
    }
});

// The sleep keeps the scheduler from a full deadlock, so the collector runs at its idle point.
delay(100);
echo "end\n";
?>
--EXPECTF--
Warning: Partial deadlock: coroutine #%d spawned at %s:%d can never wake (TaskGroup(total=1, active=1, queued=0): foreach) in %s on line %d

Warning: Partial deadlock: coroutine #%d spawned at %s:%d can never wake (Channel(capacity=0, receivers=1, senders=0, reserved receivers=0, reserved senders=0)) in %s on line %d
consumer: Deadlock detected
task 0: Deadlock detected
end
