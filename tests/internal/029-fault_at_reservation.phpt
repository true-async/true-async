--TEST--
A fatal error at the reservation for the await record, before it links: nothing is left to unlink (U3)
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
    // The reservation for the record fails before the record links.
    Test\fail_at('reserve');
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

Fatal error: Fault injected at reserve in %s on line %d
shutdown function: true true
