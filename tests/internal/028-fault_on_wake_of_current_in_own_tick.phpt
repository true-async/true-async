--TEST--
A fatal error when the waiter's own tick wakes it (a microtask cancels it): the bailout unwinds it from the tick, and its finish unlinks the record (U2)
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

$waiter = null;
$waiter = spawn(function () use ($target, &$waiter) {
    // The waiter's own tick cancels it: the enqueue of the current coroutine fails there.
    Test\defer('c', null, function () use (&$waiter) {
        echo "microtask cancels its waiter\n";
        $waiter->cancel();
    });
    Test\fail_at('enqueue');
    echo "waiter awaits\n";
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
microtask c sched=1
microtask cancels its waiter

Fatal error: Fault injected at enqueue in %s on line %d
shutdown function: true true
