--TEST--
A trigger nobody waits for keeps no coroutine from a deadlock; a holder's start counts it until its stop, and a stop of a trigger not started does nothing
--INI--
true_async.debug_deadlock=0
--FILE--
<?php
use function Async\{spawn, await, delay};
use TrueAsync\Test;

Test\trigger_new();
Test\trigger_start();
Test\trigger_start();
echo "started twice: ", Test\reactor_state()['started'], "\n";
Test\trigger_stop();
echo "one stop: ", Test\reactor_state()['started'], "\n";
Test\trigger_stop();
Test\trigger_stop();
echo "stopped: ", Test\reactor_state()['started'], "\n";

$state = new Async\FutureState();

try {
    await(new Async\Future($state));
} catch (Async\AsyncCancellation $e) {
    echo $e->getMessage(), "\n";
}
?>
--EXPECTF--
started twice: 1
one stop: 1
stopped: 0
Deadlock detected

Fatal error: Uncaught Async\DeadlockError: Deadlock detected: no active coroutines, 1 coroutines in waiting in %s
Stack trace:
#0 {main}
  thrown in %s
