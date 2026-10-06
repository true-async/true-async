--TEST--
A holder that waits with a callback of its own counts the trigger: with no coroutine on the trigger, the idle scheduler waits for the fire, the callback wakes the waiter of another event, and its stop lets the next wait be a deadlock
--INI--
true_async.debug_deadlock=0
--FILE--
<?php
use TrueAsync\Test;

Test\trigger_new();
$event = new Test\Event();
Test\trigger_relay($event);
echo "started: ", Test\reactor_state()['started'], "\n";

$started = hrtime(true);
Test\trigger_fire(100);
Test\await_records([$event]);
$elapsed = (hrtime(true) - $started) / 1e6;
echo "woken ", $elapsed >= 90 ? "through the holder" : "early, at $elapsed ms", "\n";
echo "started: ", Test\reactor_state()['started'], "\n";

try {
    Test\await_records([new Test\Event()]);
} catch (Async\AsyncCancellation $e) {
    echo $e->getMessage(), "\n";
}
?>
--EXPECTF--
started: 1
woken through the holder
started: 0
Deadlock detected

Fatal error: Uncaught Async\DeadlockError: Deadlock detected: no active coroutines, 1 coroutines in waiting in %s
Stack trace:
#0 {main}
  thrown in %s
