--TEST--
iterate(): a callback's exception cancels the other callbacks and the spawned coroutines without cancelPending
--FILE--
<?php

use function Async\delay;
use function Async\iterate;
use function Async\suspend;
use function Async\spawn;

try {
    iterate([1, 2], function ($value) {
        spawn(function () use ($value) {
            try {
                delay(10000);
            } catch (Async\AsyncCancellation $cancellation) {
                echo "child $value: ", $cancellation->getMessage(), "\n";
            }
        });
        suspend();
        if ($value === 2) {
            throw new RuntimeException("boom $value");
        }
        try {
            delay(10000);
        } catch (Async\AsyncCancellation $cancellation) {
            echo "callback $value: ", $cancellation->getMessage(), "\n";
            throw $cancellation;
        }
    }, cancelPending: false);
} catch (RuntimeException $exception) {
    echo "caught: ", $exception->getMessage(), "\n";
}
?>
--EXPECT--
callback 1: Cancellation of the iterator due to an exception
child 1: Cancellation of the iterator due to an exception
child 2: Cancellation of the iterator due to an exception
caught: boom 2
