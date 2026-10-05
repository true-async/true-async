--TEST--
One fire wakes two waiters on one event, one with two records of the event in its wait and one with one: the notify runs every record once and leaves none
--FILE--
<?php
use function Async\spawn;
use function Async\await;
use TrueAsync\Test;
use TrueAsync\Test\Event;

$event = new Event();

$twice = spawn(function () use ($event) {
    Test\await_records([$event, $event]);
    echo "twice woken\n";
});

$once = spawn(function () use ($event) {
    Test\await_records([$event]);
    echo "once woken\n";
});

Async\suspend();
echo "subscribers: ", Test\subscriber_count($event), "\n";
$event->fire();
echo "subscribers: ", Test\subscriber_count($event), "\n";
await($twice);
await($once);
?>
--EXPECT--
subscribers: 3
subscribers: 0
twice woken
once woken
