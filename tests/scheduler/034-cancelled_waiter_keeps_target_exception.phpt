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

/* Here the cancelled waiter held the only reference: the exception ends the request. */
$waiter = spawn(function () {
    $inner = spawn(function () {
        suspend();
        throw new RuntimeException("inner failed");
    });
    try {
        await($inner);
    } catch (AsyncCancellation $e) {
        echo "waiter: ", $e->getMessage(), "\n";
    }
});
suspend();
$waiter->cancel(new AsyncCancellation("waiter cancelled"));
echo "main end\n";
?>
--EXPECTF--
waiter: waiter cancelled
bool(true)
at release: target failed
main end
waiter: waiter cancelled

Fatal error: Uncaught RuntimeException: inner failed in %s:%d
Stack trace:
#0 [internal function]: {closure:{closure:%s:%d}:%d}()
#1 {main}
  thrown in %s on line %d
