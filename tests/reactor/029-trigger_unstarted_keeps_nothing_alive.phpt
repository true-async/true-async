--TEST--
A trigger nobody waits for keeps no coroutine from a deadlock
--INI--
true_async.debug_deadlock=0
--FILE--
<?php
use function Async\{await};
use TrueAsync\Test;

Test\trigger_new();

$state = new Async\FutureState();

try {
    await(new Async\Future($state));
} catch (Async\AsyncCancellation $e) {
    echo $e->getMessage(), "\n";
}
?>
--EXPECTF--
Deadlock detected

Fatal error: Uncaught Async\DeadlockError: Deadlock detected: no active coroutines, 1 coroutines in waiting in %s
Stack trace:
#0 {main}
  thrown in %s
