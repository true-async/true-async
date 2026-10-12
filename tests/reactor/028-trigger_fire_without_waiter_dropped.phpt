--TEST--
A fire that finds nobody waiting is dropped, as TrueAsync's trigger: the next waiter sleeps until the next fire; a burst of fires before a poll wakes once
--FILE--
<?php
use Async\Future;
use Async\FutureState;
use function Async\{spawn, await, delay};
use TrueAsync\Test;

Test\trigger_new();
Test\trigger_fire();
delay(10);

$started = hrtime(true);
await(spawn(function () use ($started) {
    Test\trigger_fire(150);
    Test\trigger_wait();
    $elapsed = (hrtime(true) - $started) / 1e6;
    echo "woken ", $elapsed >= 140 ? "by the later fire" : "by the dropped fire, at $elapsed ms", "\n";
}));

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
woken by the later fire
wakes after a burst: 1
