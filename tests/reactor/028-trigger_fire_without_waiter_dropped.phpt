--TEST--
A fire that finds nobody waiting is dropped, as TrueAsync's trigger: the next waiter sleeps until the next fire
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
?>
--EXPECT--
woken by the later fire
