--TEST--
graceful_shutdown() cancels the coroutines once: a second call and its cancellation are ignored
--FILE--
<?php
use function Async\spawn;
use function Async\suspend;
use function Async\graceful_shutdown;
use Async\AsyncCancellation;

$c = spawn(function () {
    try {
        suspend();
    } catch (AsyncCancellation $e) {
        echo "first: ", $e->getMessage(), "\n";
    }
    try {
        suspend();
        echo "second suspend returned\n";
    } catch (AsyncCancellation $e) {
        echo "second: ", $e->getMessage(), "\n";
    }
});
suspend();
graceful_shutdown(new AsyncCancellation("one"));

/* The first cancellation is delivered before the second call, so a second shutdown would cancel
 * the coroutine again. */
suspend();
graceful_shutdown(new AsyncCancellation("two"));
suspend();
echo "main end\n";
?>
--EXPECTF--
first: one
second suspend returned
main end
