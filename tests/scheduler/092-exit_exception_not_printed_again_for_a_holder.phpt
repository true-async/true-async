--TEST--
An exception printed as the exit exception is not printed again at the end for another coroutine that holds it
--FILE--
<?php
use function Async\await;
use function Async\spawn;
use function Async\suspend;

// The waiters park before the coroutine fails: an unawaited error of the global scope cancels
// the coroutines that have not started (dev/plans/S9-scope.md 4).
$failed = spawn(function () { suspend(); throw new RuntimeException("shared"); });
// The held waiter keeps the exception to the end; the unheld one makes it the exit exception.
$held = [spawn(fn() => await($failed))];
spawn(fn() => await($failed));
suspend();
suspend();
suspend();
echo "not reached\n";
?>
--EXPECTF--
Fatal error: Uncaught RuntimeException: shared in %s:%d
Stack trace:
#0 [internal function]: {closure:%s:%d}()
#1 {main}
  thrown in %s on line %d
