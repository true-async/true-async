--TEST--
A fatal error in main while a waiter of five records and one of two are parked: each suspend() returns with the bailout (U4), which aborts the inline records and unlinks every record, and the block is released once
--FILE--
<?php
use function Async\spawn;
use TrueAsync\Test;
use TrueAsync\Test\Event;

$events = [new Event(), new Event(), new Event(), new Event(), new Event()];

$waiter = spawn(function () use ($events) {
    try {
        Test\await_records($events, true);
    } finally {
        echo "not reached: finally\n";
    }
});

$pair = spawn(function () use ($events) {
    try {
        Test\await_records([$events[0], $events[1]], true);
    } finally {
        echo "not reached: finally\n";
    }
});

register_shutdown_function(function () use ($waiter, $pair) {
    echo "shutdown function: waiters finished ", var_export($waiter->isCompleted() && $pair->isCompleted(), true), "\n";
    var_dump(Test\wait_counters());
});

Async\suspend();
echo "main: the waiters are parked: ", var_export($waiter->isSuspended() && $pair->isSuspended(), true), "\n";
Test\fail_at('enqueue');
spawn(function () {});
echo "not reached: main\n";
?>
--EXPECTF--
main: the waiters are parked: true

Fatal error: Fault injected at enqueue in %s on line %d
shutdown function: waiters finished true
array(3) {
  ["block_releases"]=>
  int(1)
  ["typed_unlinks"]=>
  int(7)
  ["aborts"]=>
  int(2)
}
