--TEST--
A coroutine queued with an error: a cancellation over a plain error goes on top of it, a cancellation enqueued over a plain error is dropped, a plain error over a cancellation goes on top
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
/* A cancellation enqueued over a plain error is dropped. */
$b = spawn(function () {
    try { suspend(); } catch (Throwable $e) {
        echo "b: ", get_class($e), " ", $e->getMessage(), " <- ", var_export($e->getPrevious(), true), "\n";
    }
});
/* A plain error over a pending cancellation goes on top. */
$c = spawn(function () {
    try { suspend(); } catch (Throwable $e) {
        echo "c: ", get_class($e), " ", $e->getMessage(), " <- ", $e->getPrevious()?->getMessage(), "\n";
    }
});
suspend();
var_dump(Test\enqueue_with_error($a, new LogicException("plain a")));
$a->cancel(new AsyncCancellation("cancel a"));
var_dump(Test\enqueue_with_error($b, new LogicException("plain b")));
var_dump(Test\enqueue_with_error($b, new AsyncCancellation("cancel b")));
$c->cancel(new AsyncCancellation("cancel c"));
var_dump(Test\enqueue_with_error($c, new LogicException("plain c")));
suspend();
echo "main end\n";
?>
--EXPECTF--
bool(true)
bool(true)
bool(true)
bool(true)
a: Async\AsyncCancellation cancel a <- plain a
b: LogicException plain b <- NULL
c: LogicException plain c <- cancel c
main end
