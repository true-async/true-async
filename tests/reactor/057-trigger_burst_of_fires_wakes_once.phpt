--TEST--
A burst of fires of a trigger before a poll wakes its waiter once
--FILE--
<?php
use Async\Future;
use Async\FutureState;
use function Async\{spawn, await, delay};
use TrueAsync\Test;

Test\trigger_new();

$wakes = 0;
$waiting = false;
$first_wake = new FutureState();
$waiter = spawn(function () use (&$wakes, &$waiting, $first_wake) {
    $waiting = true;
    Test\trigger_wait();
    $wakes++;
    $first_wake->complete(null);
    Test\trigger_wait();
    $wakes++;
});

// A burst before the waiter links would be dropped.
while (!$waiting) {
    Async\suspend();
}

Test\trigger_fire();
Test\trigger_fire();
Test\trigger_fire();
await(new Future($first_wake));
// The polls of this wait would deliver a second wake of the burst.
delay(20);
echo "wakes after a burst: $wakes\n";
$waiter->cancel();
?>
--EXPECT--
wakes after a burst: 1
