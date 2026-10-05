--TEST--
A cancel that reaches a waiter its event already woke, before it runs: the wait is unlinked once, and the waiter takes the cancellation as a coroutine woken twice in one tick takes one wake and one error (D26)
--FILE--
<?php
use function Async\spawn;
use function Async\await;
use TrueAsync\Test;
use TrueAsync\Test\Event;

$fired = new Event();
$other = new Event();

$waiter = spawn(function () use ($fired, $other) {
    try {
        Test\await_records([$fired, $other, $other]);
        echo "woken\n";
    } catch (Async\AsyncCancellation $e) {
        echo "cancelled\n";
    }
});

Async\suspend();
$fired->fire();
$waiter->cancel();
echo "subscribers: ", Test\subscriber_count($fired), " ", Test\subscriber_count($other), "\n";
await($waiter);
echo "block releases: ", Test\wait_counters()['block_releases'], "\n";
?>
--EXPECT--
subscribers: 0 0
cancelled
block releases: 1
