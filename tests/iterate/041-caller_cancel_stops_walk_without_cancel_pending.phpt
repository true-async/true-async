--TEST--
iterate(): a cancel of the caller stops the walk without cancelPending and is thrown
--FILE--
<?php

use function Async\await;
use function Async\delay;
use function Async\iterate;
use function Async\spawn;
use function Async\suspend;

$parked = false;

$caller = spawn(function () use (&$parked) {
    try {
        iterate([1, 2, 3], function ($value) use (&$parked) {
            echo "begin $value\n";
            $parked = true;
            try {
                delay(10000);
            } catch (Async\AsyncCancellation $cancellation) {
                echo "callback $value: ", $cancellation->getMessage(), "\n";
                throw $cancellation;
            }
        }, concurrency: 1, cancelPending: false);
        echo "iterate returned\n";
    } catch (Async\AsyncCancellation $cancellation) {
        echo "caller: ", $cancellation->getMessage(), "\n";
    }
});

while (!$parked) {
    suspend();
}

$caller->cancel(new Async\AsyncCancellation('stop'));
await($caller);
echo "end\n";
?>
--EXPECT--
begin 1
callback 1: stop
caller: stop
end
