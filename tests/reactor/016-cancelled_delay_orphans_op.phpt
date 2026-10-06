--TEST--
delay(): a cancelled delay withdraws its Timer op from the queue and leaves the waits list
--FILE--
<?php
use function Async\{spawn, await, delay};
use TrueAsync\Test;

Test\reactor_use_poll_queue();

$sleeper = spawn(function () {
    try {
        delay(60000);
    } catch (Async\AsyncCancellation $e) {
        echo "cancelled: ", $e->getMessage(), "\n";
    }
});

Async\suspend();
$state = Test\reactor_state();
echo "parked: waits ", $state['waits'], ", pending ", $state['pending'], "\n";

$sleeper->cancel(new Async\AsyncCancellation("stop"));
await($sleeper);

$state = Test\reactor_state();
echo "after the cancel: waits ", $state['waits'], ", pending ", $state['pending'], "\n";
?>
--EXPECT--
parked: waits 1, pending 1
cancelled: stop
after the cancel: waits 0, pending 0
