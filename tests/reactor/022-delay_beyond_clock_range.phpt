--TEST--
delay(): a delay past the clock's range parks until cancelled instead of failing on the queue
--FILE--
<?php
use function Async\{spawn, await, delay};
use TrueAsync\Test;

$sleeper = spawn(function () {
    try {
        delay(PHP_INT_MAX);
    } catch (Async\AsyncCancellation $e) {
        echo "cancelled\n";
    }
});

Async\suspend();
echo "parked: waits ", Test\reactor_state()['waits'], "\n";
$sleeper->cancel();
await($sleeper);
?>
--EXPECT--
parked: waits 1
cancelled
