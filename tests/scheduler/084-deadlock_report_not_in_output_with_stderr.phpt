--TEST--
With display_errors=stderr the deadlock report goes to stderr with the error it explains, not to the output
--INI--
true_async.debug_deadlock=1
display_errors=stderr
log_errors=0
--CAPTURE_STDIO--
STDOUT
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
--EXPECT--
main end
Async\AsyncCancellation: Deadlock detected
Async\AsyncCancellation: Deadlock detected
