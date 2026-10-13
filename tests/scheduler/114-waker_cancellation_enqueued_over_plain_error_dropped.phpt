--TEST--
A coroutine queued with an error: a cancellation enqueued over a plain error is dropped
--FILE--
<?php
use function Async\spawn;
use function Async\suspend;
use TrueAsync\Test;
use Async\AsyncCancellation;

/* A cancellation enqueued over a plain error is dropped. */
$b = spawn(function () {
    try { suspend(); } catch (Throwable $e) {
        echo "b: ", get_class($e), " ", $e->getMessage(), " <- ", var_export($e->getPrevious(), true), "\n";
    }
});
suspend();
var_dump(Test\enqueue_with_error($b, new LogicException("plain b")));
var_dump(Test\enqueue_with_error($b, new AsyncCancellation("cancel b")));
suspend();
echo "main end\n";
?>
--EXPECTF--
bool(true)
bool(true)
b: LogicException plain b <- NULL
main end
