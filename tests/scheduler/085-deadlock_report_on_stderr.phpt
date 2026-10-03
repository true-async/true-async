--TEST--
With display_errors=stderr the deadlock report goes to stderr with the error it explains
--INI--
true_async.debug_deadlock=1
display_errors=stderr
log_errors=0
--CAPTURE_STDIO--
STDERR
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
%A=== DEADLOCK REPORT START ===
%A=== DEADLOCK REPORT END   ===
%A
Fatal error: Uncaught Async\DeadlockError: Deadlock detected: %A
