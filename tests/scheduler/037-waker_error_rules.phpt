--TEST--
A coroutine queued with an error: a cancellation over a plain error goes on top of it
--FILE--
<?php
use function Async\spawn;
use function Async\suspend;
use TrueAsync\Test;
use Async\AsyncCancellation;

/* A cancellation over a pending plain error goes on top, the error becomes its previous. */
$a = spawn(function () {
    try { suspend(); } catch (Throwable $e) {
        echo "a: ", get_class($e), " ", $e->getMessage(), " <- ", $e->getPrevious()?->getMessage(), "\n";
    }
});
suspend();
var_dump(Test\enqueue_with_error($a, new LogicException("plain a")));
$a->cancel(new AsyncCancellation("cancel a"));
suspend();
echo "main end\n";
?>
--EXPECTF--
bool(true)
a: Async\AsyncCancellation cancel a <- plain a
main end
