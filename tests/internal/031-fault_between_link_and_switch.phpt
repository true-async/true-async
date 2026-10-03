--TEST--
A fatal error after the await record links and before the waiter switches away: the bailout unwinds the waiter on its own stack, and its finish unlinks the record (U5)
--FILE--
<?php
use function Async\spawn;
use function Async\await;
use TrueAsync\Test;

$target = spawn(function () {
    echo "target yields\n";
    Async\suspend();
    echo "not reached: target\n";
});

$waiter = spawn(function () use ($target) {
    echo "waiter awaits\n";
    // The record is linked into the target, and the waiter has not switched yet.
    Test\fail_at('link');
    try {
        await($target);
    } finally {
        echo "not reached: finally\n";
    }
});

register_shutdown_function(function () use ($target, $waiter) {
    echo "shutdown function: ", var_export($target->isCompleted(), true), " ", var_export($waiter->isCompleted(), true), "\n";
});

echo "main end\n";
?>
--EXPECTF--
main end
target yields
waiter awaits

Fatal error: Fault injected at link in %s on line %d
shutdown function: true true
