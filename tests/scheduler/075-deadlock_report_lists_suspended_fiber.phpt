--TEST--
The deadlock report lists a suspended Fiber beside a real deadlock, waiting for nothing
--INI--
true_async.debug_deadlock=1
--FILE--
<?php
use function Async\spawn;
use function Async\await;

$fiber = new Fiber(function () {
    Fiber::suspend();
});
$fiber->start();

$first = spawn(function () use (&$second) {
    await($second);
});

$second = spawn(function () use (&$first) {
    await($first);
});

echo "main end\n";
?>
--EXPECTF--
main end

=== DEADLOCK REPORT START ===
Coroutines waiting: 3

Coroutine %d spawned at %s, suspended at %s:6
  waiting for: <nothing>

Coroutine %d spawned at %s:10, suspended at %s:11
  waiting for:
    - await: coroutine #%d

Coroutine %d spawned at %s:14, suspended at %s:15
  waiting for:
    - await: coroutine #%d

=== DEADLOCK REPORT END   ===


Fatal error: Uncaught Async\DeadlockError: Deadlock detected: no active coroutines, 3 coroutines in waiting in [no active file]:0
Stack trace:
#0 {main}
  thrown in [no active file] on line 0
