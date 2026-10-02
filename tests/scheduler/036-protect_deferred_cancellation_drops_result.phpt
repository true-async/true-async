--TEST--
A cancellation deferred by protect() is thrown when it returns, the protected closure's result is released, and a second cancel() is dropped
--FILE--
<?php
use function Async\spawn;
use function Async\suspend;
use function Async\protect;
use function Async\await;
use Async\AsyncCancellation;

class Result
{
    public function __destruct()
    {
        echo "result released\n";
    }
}

$c = spawn(function () {
    try {
        $value = protect(function () {
            suspend();
            return new Result();
        });
        echo "not reached\n";
    } catch (AsyncCancellation $e) {
        echo "after protect: ", $e->getMessage(), "\n";
    }
    return "done";
});
suspend();
$c->cancel(new AsyncCancellation("deferred"));
$c->cancel(new AsyncCancellation("second, dropped"));
var_dump($c->isCancellationRequested());
echo await($c), "\n";
?>
--EXPECTF--
bool(true)
result released
after protect: deferred
done
