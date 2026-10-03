--TEST--
Finish handlers behind one that throws are never called: the notify stops at the throw and they are disposed with the vector
--FILE--
<?php
use function Async\spawn;
use function Async\await;

/* Both ahead of main's wait record: the first throws, so the second is disposed uncalled, and main is
 * woken by the graceful shutdown that the exception starts. */
$coroutine = spawn(function () {
    return 1;
});
TrueAsync\Test\add_throwing_finish_handler($coroutine);
TrueAsync\Test\add_throwing_finish_handler($coroutine);

try {
    await($coroutine);
} catch (Throwable $e) {
    echo get_class($e), ": ", $e->getMessage(), "\n";
}

echo "main end\n";
?>
--EXPECTF--
Async\AsyncCancellation: Graceful shutdown
main end

Fatal error: Uncaught Exception: finish handler in %s:%d
Stack trace:
#0 {main}
  thrown in %s on line %d
