--TEST--
Timeout::isCompleted() past the deadline fires the Timeout while a waiter is parked on it and its timer armed: the waiter wakes, the timer is withdrawn
--FILE--
<?php

use Async\FutureState;
use Async\Future;
use Async\OperationCanceledException;
use function Async\await;
use function Async\spawn;
use function Async\suspend;
use function Async\timeout;
use TrueAsync\Test;

$timeout = timeout(20);
$never = new FutureState();
$never->ignore();

$waiter = spawn(function () use ($never, $timeout) {
    try {
        await(new Future($never), $timeout);
    } catch (OperationCanceledException $e) {
        echo "waiter: ", $e->getPrevious()->getMessage(), "\n";
    }
});

suspend();
echo "armed: waits ", Test\reactor_state()['waits'], "\n";
// A spin, not usleep(), which parks main and lets the timer fire.
$until = hrtime(true) + 40_000_000;
while (hrtime(true) < $until);
var_dump($timeout->isCompleted());
echo "fired: waits ", Test\reactor_state()['waits'], "\n";
await($waiter);

?>
--EXPECT--
armed: waits 1
bool(true)
fired: waits 0
waiter: Timeout occurred after 20 milliseconds
