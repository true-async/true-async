--TEST--
A waiter whose record a throwing finish handler left behind is woken by the teardown, ahead of the graceful shutdown that the handler's exception starts
--FILE--
<?php
use function Async\spawn;
use function Async\await;
use function Async\suspend;
use Async\AsyncCancellation;
use TrueAsync\Test;

$target = spawn(function () {
    suspend();
    return "result";
});

/* Ahead of the waiter's record in the target's callbacks. */
Test\add_throwing_finish_handler($target);

/* Parked on each other: the shutdown queues them in spawn order, behind a waiter that the teardown
 * already woke, ahead of one it did not. */
$first = null;

$second = spawn(function () use (&$first) {
    try {
        await($first);
    } catch (AsyncCancellation $e) {
        echo "second: ", $e->getMessage(), "\n";
    }
});

$first = spawn(function () use ($second) {
    try {
        await($second);
    } catch (AsyncCancellation $e) {
        echo "first: ", $e->getMessage(), "\n";
    }
});

spawn(function () use ($target) {
    try {
        echo "waiter: ", await($target), "\n";
    } catch (AsyncCancellation $e) {
        echo $e->getMessage(), ", target: ", $target->getResult(), "\n";
    }
});

echo "main end\n";
?>
--EXPECTF--
main end
waiter: Graceful shutdown, target: result
second: Graceful shutdown
first: Graceful shutdown

Fatal error: Uncaught Exception: finish handler in %s:%d
Stack trace:
#0 {main}
  thrown in %s on line %d
