--TEST--
A cancel wakes a waiter of one, two or five records: every record leaves its event, which a later waiter still gets alone, and the block is released once
--FILE--
<?php
use function Async\spawn;
use function Async\await;
use TrueAsync\Test;
use TrueAsync\Test\Event;

foreach ([1, 2, 5] as $count) {
    $event = new Event();

    $waiter = spawn(function () use ($event, $count) {
        try {
            Test\await_records(array_fill(0, $count, $event));
        } catch (Async\AsyncCancellation $e) {
            echo "$count records: cancelled\n";
        }
    });

    Async\suspend();
    $waiter->cancel();
    await($waiter);
    echo "subscribers: ", Test\subscriber_count($event), "\n";

    $second = spawn(function () use ($event) {
        Test\await_records([$event]);
        echo "second woken\n";
    });

    Async\suspend();
    $event->fire();
    await($second);
}

echo "block releases: ", Test\wait_counters()['block_releases'], "\n";
?>
--EXPECT--
1 records: cancelled
subscribers: 0
second woken
2 records: cancelled
subscribers: 0
second woken
5 records: cancelled
subscribers: 0
second woken
block releases: 1
