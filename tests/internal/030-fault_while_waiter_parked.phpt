--TEST--
A fatal error in main while a waiter is parked with its record linked into a target not yet unwound: the waiter's suspend() returns with the bailout (U4) and its unwinding removes the record with no crash
--FILE--
<?php
use function Async\spawn;
use function Async\await;
use TrueAsync\Test;

// The bailout unwinds coroutines in spawn order: the waiter goes first, its record still linked
// into the target spawned after it.
$target = null;

$waiter = spawn(function () use (&$target) {
    echo "waiter awaits\n";
    try {
        await($target);
    } finally {
        echo "not reached: finally\n";
    }
});

$target = spawn(function () {
    echo "target yields\n";
    Async\suspend();
    echo "not reached: target\n";
});

register_shutdown_function(function () use ($target, $waiter) {
    echo "shutdown function: ", var_export($target->isCompleted(), true), " ", var_export($waiter->isCompleted(), true), "\n";
});

Async\suspend();
echo "main: the waiter is parked: ", var_export($waiter->isSuspended(), true), "\n";
// Main fails while the waiter is parked: the waiter's suspend() returns with the bailout.
Test\fail_at('enqueue');
spawn(function () {});
echo "not reached: main\n";
?>
--EXPECTF--
waiter awaits
target yields
main: the waiter is parked: true

Fatal error: Fault injected at enqueue in %s on line %d
shutdown function: true true
