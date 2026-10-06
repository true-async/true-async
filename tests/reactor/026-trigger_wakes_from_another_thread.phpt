--TEST--
A coroutine parked on a trigger is woken by a fire from another thread: the idle scheduler waits for it instead of resolving a deadlock, and other coroutines run meanwhile
--FILE--
<?php
use function Async\{spawn, await, delay};
use TrueAsync\Test;

Test\trigger_new();
$started = hrtime(true);

$waiter = spawn(function () use ($started) {
    Test\trigger_fire(150);
    Test\trigger_wait();
    $elapsed = (hrtime(true) - $started) / 1e6;
    echo "woken ", $elapsed >= 140 ? "after the fire" : "early, at $elapsed ms", "\n";
});

spawn(function () {
    delay(20);
    $state = Test\reactor_state();
    echo "while parked: started ", $state['started'], ", triggers ", $state['triggers'], "\n";
});

await($waiter);
$state = Test\reactor_state();
echo "after: started ", $state['started'], ", triggers ", $state['triggers'], ", own ", $state['own'], "\n";
?>
--EXPECT--
while parked: started 1, triggers 1
woken after the fire
after: started 0, triggers 1, own 1
