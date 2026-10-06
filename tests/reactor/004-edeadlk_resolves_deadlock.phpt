--TEST--
A wait the Poll queue answers with EDEADLK (a Timer op that never fires) is a deadlock: the waiter is cancelled and named in the report
--INI--
true_async.debug_deadlock=1
--FILE--
<?php
use function Async\spawn;
use TrueAsync\Test;

Test\reactor_use_poll_queue();

spawn(function () {
    try {
        Test\reactor_wait(-1);
    } catch (Async\AsyncCancellation $e) {
        echo $e->getMessage(), ", waits: ", Test\reactor_state()['waits'], "\n";
    }
});
?>
--EXPECTF--
=== DEADLOCK REPORT START ===
Coroutines waiting: 1

Coroutine %d spawned at %s:%d, suspended at %s:%d
  waiting for:
    - reactor wait: timer

=== DEADLOCK REPORT END   ===

Deadlock detected, waits: 0

Fatal error: Uncaught Async\DeadlockError: Deadlock detected: no active coroutines, 1 coroutines in waiting in %s
Stack trace:
#0 {main}
  thrown in %s
