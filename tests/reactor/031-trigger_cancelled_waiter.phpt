--TEST--
A cancelled trigger waiter leaves: the trigger stops counting, a later fire wakes nobody, and the trigger serves the next waiter
--FILE--
<?php
use function Async\{spawn, await, delay};
use TrueAsync\Test;

Test\trigger_new();

$waiter = spawn(function () {
    try {
        Test\trigger_wait();
        echo "woken\n";
    } catch (Async\AsyncCancellation $e) {
        echo "cancelled\n";
    }
});

Async\suspend();
echo "parked: ", Test\reactor_state()['started'], "\n";
$waiter->cancel();
Async\suspend();
echo "after the cancel: ", Test\reactor_state()['started'], "\n";
Test\trigger_fire();
delay(10);

await(spawn(function () {
    Test\trigger_fire(30);
    Test\trigger_wait();
    echo "next waiter woken\n";
}));
?>
--EXPECT--
parked: 1
cancelled
after the cancel: 0
next waiter woken
