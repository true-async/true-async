--TEST--
A fatal error on the wake path of a parked waiter (U1), before its unlink: the record stays linked into a target whose notify the bailout cut, and the waiter's unwinding removes it with no crash
--FILE--
<?php
use function Async\spawn;
use function Async\await;
use TrueAsync\Test;

$target = spawn(function () {
    echo "target runs\n";
    Async\suspend();
    echo "target ends\n";
    // Its finish wakes the parked waiter: the enqueue fails there.
    Test\fail_at('enqueue');
});

$waiter = spawn(function () use ($target) {
    echo "waiter awaits\n";
    try {
        await($target);
    } finally {
        echo "not reached: finally\n";
    }
});

register_shutdown_function(function () use ($waiter) {
    echo "shutdown function: waiter finished ", var_export($waiter->isCompleted(), true), "\n";
});

echo "main end\n";
?>
--EXPECTF--
main end
target runs
waiter awaits
target ends

Fatal error: Fault injected at enqueue in %s on line %d
shutdown function: waiter finished true
