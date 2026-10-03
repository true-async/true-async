--TEST--
The deadlock report is not printed when display_errors is off, as the errors it explains are not; the error is still logged
--INI--
true_async.debug_deadlock=1
display_errors=0
log_errors=1
error_log=
--FILE--
<?php
use function Async\spawn;
use function Async\await;

$first = spawn(function () use (&$second) {
    try {
        await($second);
    } catch (Throwable $error) {
        echo get_class($error), ": ", $error->getMessage(), "\n";
    }
});

$second = spawn(function () use (&$first) {
    try {
        await($first);
    } catch (Throwable $error) {
        echo get_class($error), ": ", $error->getMessage(), "\n";
    }
});

echo "main end\n";
?>
--EXPECTF--
main end
Async\AsyncCancellation: Deadlock detected
Async\AsyncCancellation: Deadlock detected
PHP Fatal error:  Uncaught Async\DeadlockError: Deadlock detected: %s
Stack trace:
#0 {main}
  thrown in %s on line %d
