--TEST--
A waiter whose record a throwing subscriber of a Timeout left behind is woken by the fire, ahead of the graceful shutdown that the subscriber's exception starts
--FILE--
<?php
use function Async\spawn;
use function Async\await;
use function Async\timeout;
use Async\FutureState;
use Async\Future;
use Async\OperationCanceledException;
use TrueAsync\Test;

/* Long enough for the waiter to subscribe first on a loaded machine: an await() that finds the
 * deadline passed fires the Timeout itself, and the subscriber's exception then joins the waiter's. */
$timeout = timeout(1000);

/* Ahead of the waiter's record in the Timeout's callbacks. */
Test\add_throwing_subscriber($timeout);

$waiter = spawn(function () use ($timeout) {
    try {
        await(new Future(new FutureState()), $timeout);
    } catch (OperationCanceledException $e) {
        echo "waiter: ", get_class($e->getPrevious()), "\n";
    }
});

try {
    await($waiter);
} catch (Throwable $e) {
    echo "main: ", get_class($e), ": ", $e->getMessage(), "\n";
}

echo "end\n";
?>
--EXPECT--
waiter: Async\TimeoutException
main: Async\AsyncCancellation: Graceful shutdown
end

Fatal error: Uncaught Exception: subscriber in [no active file]:0
Stack trace:
#0 {main}
  thrown in [no active file] on line 0
