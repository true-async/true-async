--TEST--
The deadlock report reaches an output handler in one write; the handler cannot wait there, and the deadlocked coroutines are cancelled after it
--INI--
true_async.debug_deadlock=1
--FILE--
<?php
use function Async\spawn;
use function Async\await;

$a = null;
$b = spawn(function () use (&$a) {
    try { await($a); } catch (Async\AsyncCancellation $e) { echo "b: ", $e->getMessage(), "\n"; }
});
$a = spawn(function () use ($b) {
    try { await($b); } catch (Async\AsyncCancellation $e) { echo "a: ", $e->getMessage(), "\n"; }
});

ob_start(function ($buffer) use (&$a) {
    static $done = false;
    if (!$done && str_contains($buffer, 'DEADLOCK REPORT START')) {
        $done = true;
        $note = "[one write: " . var_export(str_contains($buffer, 'DEADLOCK REPORT END'), true) . "]\n";
        try {
            await($a);
            $note .= "[handler awaited]\n";
        } catch (Throwable $e) {
            $note .= "[handler: " . get_class($e) . ": " . $e->getMessage() . "]\n";
        }
        return $buffer . $note;
    }
    return $buffer;
}, 1);
echo "main end\n";
?>
--EXPECTF--
main end

=== DEADLOCK REPORT START ===
Coroutines waiting: 2

Coroutine %d spawned at %s:%d, suspended at %s:%d
  waiting for:
    - await: coroutine #%d

Coroutine %d spawned at %s:%d, suspended at %s:%d
  waiting for:
    - await: coroutine #%d

=== DEADLOCK REPORT END   ===

[one write: true]
[handler: Error: The operation cannot be executed in the scheduler context]
b: Deadlock detected
a: Deadlock detected

Fatal error: Uncaught Async\DeadlockError: Deadlock detected: no active coroutines, 2 coroutines in waiting in [no active file]:0
Stack trace:
#0 {main}
  thrown in [no active file] on line %d
