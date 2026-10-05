--TEST--
A fatal error on the wake of a waiter whose wait has a block (U1): the bailout leaves its five typed records linked, its unwinding unlinks each through its kind, and the block is released once
--FILE--
<?php
use function Async\spawn;
use TrueAsync\Test;
use TrueAsync\Test\Event;

$events = [new Event(), new Event(), new Event(), new Event()];

$target = spawn(function () {
    Async\suspend();
    echo "target ends\n";
    Test\fail_at('enqueue');
});

$waiter = spawn(function () use ($target, $events) {
    try {
        Test\await_records([...$events, $target], true);
    } finally {
        echo "not reached: finally\n";
    }
});

register_shutdown_function(function () use ($waiter, $events) {
    echo "shutdown function: waiter finished ", var_export($waiter->isCompleted(), true), "\n";
    echo "subscribers: ", implode(" ", array_map(fn($event) => Test\subscriber_count($event), $events)), "\n";
    var_dump(Test\wait_counters());
});

echo "main end\n";
?>
--EXPECTF--
main end
target ends

Fatal error: Fault injected at enqueue in %s on line %d
shutdown function: waiter finished true
subscribers: 0 0 0 0
array(3) {
  ["block_releases"]=>
  int(1)
  ["typed_unlinks"]=>
  int(5)
  ["aborts"]=>
  int(0)
}
