--TEST--
A fatal error in the deadlock resolution reaches a coroutine that parked with nothing queued, on the scheduler coroutine: its suspend() returns with the bailout and it unwinds with the rest
--INI--
true_async.debug_deadlock=0
--FILE--
<?php
use function Async\spawn;
use function Async\await;
use TrueAsync\Test;

$first = null;
$second = null;

$first = spawn(function () use (&$second) {
    try {
        await($second);
    } finally {
        echo "first unwinds\n";
    }
});

// Parks last, with nothing queued: it waits on the scheduler coroutine, not on a direct switch.
$second = spawn(function () use (&$first) {
    try {
        Test\fail_at('enqueue');
        await($first);
    } finally {
        echo "second unwinds\n";
    }
});

await($first);
echo "not reached: main\n";
?>
--EXPECTF--
Fatal error: Fault injected at enqueue in %s on line %d
