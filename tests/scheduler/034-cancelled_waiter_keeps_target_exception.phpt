--TEST--
A waiter cancelled before its target fails does not count as observing the target's exception
--FILE--
<?php
use function Async\spawn;
use function Async\await;
use function Async\suspend;
use Async\AsyncCancellation;

/* The waiter is cancelled before the target fails: nobody observed the target's exception. */
$target = spawn(function () {
    suspend();
    throw new RuntimeException("target failed");
});
$waiter = spawn(function () use ($target) {
    try {
        await($target);
    } catch (AsyncCancellation $e) {
        echo "waiter: ", $e->getMessage(), "\n";
    }
});
suspend();
$waiter->cancel(new AsyncCancellation("waiter cancelled"));
suspend();
suspend();
var_dump($target->isCompleted());

try {
    unset($target);
} catch (RuntimeException $e) {
    echo "at release: ", $e->getMessage(), "\n";
}
?>
--EXPECT--
waiter: waiter cancelled
bool(true)
at release: target failed
