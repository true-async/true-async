--TEST--
D16: the deadline's Timer is no coroutine's wait, so a deadlock during the graceful shutdown is resolved at once
--FILE--
<?php
use function Async\{spawn, await, delay};

$started = hrtime(true);
$a = null;
$b = null;

$a = spawn(function () use (&$b) {
    try {
        delay(60000);
    } catch (Async\AsyncCancellation $e) {
        echo "a waits for b\n";
        await($b);
    }
});

$b = spawn(function () use (&$a) {
    try {
        delay(60000);
    } catch (Async\AsyncCancellation $e) {
        echo "b waits for a\n";
        await($a);
    }
});

register_shutdown_function(function () use ($started) {
    $elapsed = (hrtime(true) - $started) / 1e6;
    echo $elapsed < 4000 ? "resolved before the deadline" : "resolved after $elapsed ms", "\n";
});

Async\suspend();
exit(0);
?>
--EXPECTF--
a waits for b
b waits for a
%A
Fatal error: Uncaught Async\DeadlockError: %a
resolved before the deadline
