--TEST--
A coroutine queued with an error: a plain error over a cancellation goes on top
--FILE--
<?php
use function Async\spawn;
use function Async\suspend;
use TrueAsync\Test;
use Async\AsyncCancellation;

/* A plain error over a pending cancellation goes on top. */
$c = spawn(function () {
    try { suspend(); } catch (Throwable $e) {
        echo "c: ", get_class($e), " ", $e->getMessage(), " <- ", $e->getPrevious()?->getMessage(), "\n";
    }
});
suspend();
$c->cancel(new AsyncCancellation("cancel c"));
var_dump(Test\enqueue_with_error($c, new LogicException("plain c")));
suspend();
echo "main end\n";
?>
--EXPECTF--
bool(true)
c: LogicException plain c <- cancel c
main end
