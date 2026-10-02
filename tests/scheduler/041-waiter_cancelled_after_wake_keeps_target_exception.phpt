--TEST--
A waiter woken by its target's failure and cancelled before it reads the outcome does not count as observing the target's exception
--FILE--
<?php
use function Async\spawn;
use function Async\await;
use function Async\suspend;
use Async\AsyncCancellation;

$target = spawn(function () {
    suspend();
    throw new RuntimeException("target failed");
});
$waiter = null;
$waiter = spawn(function () use ($target) {
    try {
        await($target);
        echo "not reached\n";
    } catch (AsyncCancellation $e) {
        echo "waiter: ", $e->getMessage(), "\n";
    }
});
/* Runs after the target fails and wakes the waiter, before the waiter reads the outcome. */
spawn(function () use (&$waiter) {
    suspend();
    $waiter->cancel(new AsyncCancellation("cancelled after the wake"));
});
suspend();
suspend();
suspend();

try {
    unset($target);
} catch (RuntimeException $e) {
    echo "at release: ", $e->getMessage(), "\n";
}

echo "main end\n";
?>
--EXPECTF--
waiter: cancelled after the wake
at release: target failed
main end
