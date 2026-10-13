--TEST--
iterate(): a spawned coroutine's error during the walk stops it and is thrown by iterate()
--FILE--
<?php

use function Async\await;
use function Async\delay;
use function Async\iterate;
use function Async\spawn;
use function Async\suspend;

$sibling = spawn(function () {
    suspend();
    suspend();
    echo "sibling finished\n";
});

$caller = spawn(function () {
    try {
        iterate([1, 2], function ($value) {
            if ($value === 1) {
                spawn(function () {
                    throw new RuntimeException('child failed');
                });
            }
            try {
                delay(10000);
            } catch (Async\AsyncCancellation $cancellation) {
                echo "callback $value: ", $cancellation->getMessage(), "\n";
                throw $cancellation;
            }
        }, concurrency: 1, cancelPending: false);
        echo "iterate returned\n";
    } catch (RuntimeException $exception) {
        echo "caught: ", $exception->getMessage(), "\n";
    }
    echo "caller goes on\n";
});

await($caller);
await($sibling);
echo "end\n";
?>
--EXPECT--
sibling finished
callback 1: Coroutine cancelled
caught: child failed
caller goes on
end
