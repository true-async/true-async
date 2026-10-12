--TEST--
D16: the deadline's Timer is no coroutine's wait, so a deadlock during the graceful shutdown is resolved at once
--DESCRIPTION--
A deadline that fired would cancel both coroutines with a graceful exit no catch sees and raise no
DeadlockError, so the error shows the deadlock was found first.
--FILE--
<?php
use function Async\{spawn, await, delay};
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

Async\suspend();
exit(0);
?>
--EXPECTF--
a waits for b
b waits for a
%A
Fatal error: Uncaught Async\DeadlockError: %a
