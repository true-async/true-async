--TEST--
The deadlock report lists the waiters only, not a core coroutine the scheduler refused to enqueue
--INI--
true_async.debug_deadlock=1
display_errors=1
--FILE--
<?php
use function Async\spawn;
use function Async\await;

ini_set('fiber.stack_size', '1048576G'); // 1 PiB: beyond what mmap and VirtualAlloc can place, refused everywhere

$fiber = new Fiber(function () {
    echo "not reached\n";
});

try {
    $fiber->start();
} catch (Exception $e) {
    echo "refused\n";
}

ini_restore('fiber.stack_size');

$first = spawn(function () use (&$second) {
    try {
        await($second);
    } catch (Throwable $error) {
        echo get_class($error), "\n";
    }
});

$second = spawn(function () use (&$first) {
    try {
        await($first);
    } catch (Throwable $error) {
        echo get_class($error), "\n";
    }
});

var_dump(count(Async\get_coroutines()));
echo "main end\n";
?>
--EXPECTF--
refused
int(3)
main end

=== DEADLOCK REPORT START ===
Coroutines waiting: 2

Coroutine %d spawned at %s:19, suspended at %s:21
  waiting for:
    - await: coroutine #%d

Coroutine %d spawned at %s:27, suspended at %s:29
  waiting for:
    - await: coroutine #%d

=== DEADLOCK REPORT END   ===

Async\AsyncCancellation
Async\AsyncCancellation

Fatal error: Uncaught Async\DeadlockError: Deadlock detected: no active coroutines, 2 coroutines in waiting in [no active file]:0
Stack trace:
#0 {main}
  thrown in [no active file] on line 0
