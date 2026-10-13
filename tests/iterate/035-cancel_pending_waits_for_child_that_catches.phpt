--TEST--
iterate(): cancelPending waits for a spawned coroutine that catches the cancellation
--FILE--
<?php

use function Async\delay;
use function Async\iterate;
use function Async\suspend;
use function Async\spawn;

iterate([1], function () {
    spawn(function () {
        try {
            delay(10000);
        } catch (Async\AsyncCancellation $cancellation) {
            echo "child: ", $cancellation->getMessage(), "\n";
            suspend();
            echo "child finished after the cancel\n";
        }
    });
});

echo "iterate returned\n";
?>
--EXPECT--
child: Cancellation of pending coroutines after iterator completion
child finished after the cancel
iterate returned
