--TEST--
A wait of one and two records in the waker and of five in a block: each line of the awaiting info, a wake by the last event, no record left on any event, the block released once
--FILE--
<?php
use function Async\spawn;
use function Async\await;
use TrueAsync\Test;
use TrueAsync\Test\Event;

foreach ([1, 2, 5] as $count) {
    $events = [];

    for ($i = 0; $i < $count; $i++) {
        $events[] = new Event();
    }

    $waiter = spawn(function () use ($events) {
        Test\await_records($events);
        echo "woken\n";
    });

    Async\suspend();
    echo "$count records: ", count($waiter->getAwaitingInfo()), " lines\n";
    echo "subscribers while parked: ", implode(" ", array_map(fn($event) => Test\subscriber_count($event), $events)), "\n";
    $events[$count - 1]->fire();
    await($waiter);
    echo "subscribers after the wake: ", implode(" ", array_map(fn($event) => Test\subscriber_count($event), $events)), "\n";

    echo "block releases: ", Test\wait_counters()['block_releases'], "\n";
}
?>
--EXPECT--
1 records: 1 lines
subscribers while parked: 1
woken
subscribers after the wake: 0
block releases: 0
2 records: 2 lines
subscribers while parked: 1 1
woken
subscribers after the wake: 0 0
block releases: 0
5 records: 5 lines
subscribers while parked: 1 1 1 1 1
woken
subscribers after the wake: 0 0 0 0 0
block releases: 1
