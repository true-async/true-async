--TEST--
A fatal error in a microtask of the waiter's own tick, with its two typed records linked: the bailout lands in the waiter's body, and its finish aborts and unlinks both records
--FILE--
<?php
use function Async\spawn;
use TrueAsync\Test;
use TrueAsync\Test\Event;

$events = [new Event(), new Event()];

$waiter = spawn(function () use ($events) {
    Test\defer('f', null, function () {
        Test\fail_at('enqueue');
        spawn(function () {});
    });

    try {
        Test\await_records($events, true);
    } finally {
        echo "not reached: finally\n";
    }
});

register_shutdown_function(function () use ($waiter, $events) {
    echo "shutdown function: waiter finished ", var_export($waiter->isCompleted(), true), "\n";
    echo "subscribers: ", implode(" ", array_map(fn($event) => Test\subscriber_count($event), $events)), "\n";
    var_dump(Test\wait_counters());
});
?>
--EXPECTF--
microtask f sched=1
released f

Fatal error: Fault injected at enqueue in %s on line %d
shutdown function: waiter finished true
subscribers: 0 0
array(3) {
  ["block_releases"]=>
  int(0)
  ["typed_unlinks"]=>
  int(2)
  ["aborts"]=>
  int(2)
}
