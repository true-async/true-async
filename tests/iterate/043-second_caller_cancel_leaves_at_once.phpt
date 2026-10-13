--TEST--
iterate(): a second cancel of the caller leaves at once, the spawned coroutine runs on
--FILE--
<?php

use function Async\await;
use function Async\delay;
use function Async\iterate;
use function Async\spawn;
use function Async\suspend;

$parked = false;
$caught = false;
$release = false;

$caller = spawn(function () use (&$parked, &$caught, &$release) {
    try {
        iterate([1], function () use (&$parked, &$caught, &$release) {
            spawn(function () use (&$parked, &$caught, &$release) {
                $parked = true;
                try {
                    delay(10000);
                } catch (Async\AsyncCancellation $cancellation) {
                    echo "child: ", $cancellation->getMessage(), "\n";
                    $caught = true;
                    while (!$release) {
                        suspend();
                    }
                    echo "child ends\n";
                }
            });
        }, cancelPending: false);
        echo "iterate returned\n";
    } catch (Async\AsyncCancellation $cancellation) {
        echo "caller: ", $cancellation->getMessage(), ", previous: ", $cancellation->getPrevious()?->getMessage(), "\n";
    }
});

while (!$parked) {
    suspend();
}

$caller->cancel(new Async\AsyncCancellation('first'));

while (!$caught) {
    suspend();
}

$caller->cancel(new Async\AsyncCancellation('second'));
await($caller);
echo "caller left\n";
$release = true;
?>
--EXPECT--
child: first
caller: first, previous: 
caller left
child ends
