--TEST--
A fire that finds nobody waiting is dropped, as TrueAsync's trigger: the next waiter sleeps until the next fire; a burst of fires before a poll wakes once
--FILE--
<?php
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
$waiter = spawn(function () use (&$wakes) {
    Test\trigger_wait();
    $wakes++;
    Test\trigger_wait();
    $wakes++;
});

Async\suspend();
Test\trigger_fire();
Test\trigger_fire();
Test\trigger_fire();
delay(20);
echo "wakes after a burst: $wakes\n";
$waiter->cancel();
?>
--EXPECT--
woken by the later fire
wakes after a burst: 1
