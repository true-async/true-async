--TEST--
An output handler that throws on the deadlock report: the exception joins the request's exit exception ahead of the deadlock error, and the deadlock resolution goes on
--INI--
true_async.debug_deadlock=1
--FILE--
<?php
use function Async\spawn;
use function Async\await;

$main = Async\current_coroutine();

$coroutine = spawn(function () use ($main) {
    try {
        await($main);
    } catch (Throwable $e) {
        echo "coroutine: ", get_class($e), ": ", $e->getMessage(), "\n";
    }
});

// A chunk size of 1 hands every write to the handler, the report's included.
ob_start(function ($buffer) {
    if (str_contains($buffer, "DEADLOCK")) {
        throw new Exception("output handler");
    }

    return $buffer;
}, 1);

try {
    await($coroutine);
} catch (Throwable $e) {
    echo "main: ", get_class($e), ": ", $e->getMessage(), "\n";
}

ob_end_flush();
echo "main end\n";
?>
--EXPECTF--
=== DEADLOCK REPORT START ===
Coroutines waiting: 2

Coroutine 1 spawned at %s, suspended at %s:25
  waiting for:
    - await: coroutine #3

Coroutine 3 spawned at %s:7, suspended at %s:9
  waiting for:
    - await: coroutine #1

=== DEADLOCK REPORT END   ===

main: Async\AsyncCancellation: Deadlock detected
main end
coroutine: Async\AsyncCancellation: Deadlock detected

Fatal error: Uncaught Exception: output handler in %s:18
Stack trace:
#0 [internal function]: {closure:%s:16}('\n=== DEADLOCK R...', 1)
#1 {main}

Next Async\DeadlockError: Deadlock detected: no active coroutines, 2 coroutines in waiting in [no active file]:0
Stack trace:
#0 {main}
  thrown in [no active file] on line 0
