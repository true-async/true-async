--TEST--
iterate(): after a callback's exception, cancelPending does not cancel again a child that handles the first cancel
--FILE--
<?php

use function Async\delay;
use function Async\iterate;
use function Async\spawn;
use function Async\suspend;

$parked = 0;

try {
    iterate([1, 2], function ($value) use (&$parked) {
        if ($value === 1) {
            spawn(function () use (&$parked) {
                try {
                    $parked++;
                    delay(10000);
                } catch (Async\AsyncCancellation $cancellation) {
                    echo "child: ", $cancellation->getMessage(), "\n";
                    delay(20);
                    echo "child cleanup done\n";
                }
            });

            // The child and the second worker park before the walk's exception cancels them.
            while ($parked < 2) {
                suspend();
            }

            throw new Exception('boom');
        }

        $parked++;
        delay(10000);
    });
} catch (Exception $exception) {
    echo "caught: ", $exception->getMessage(), "\n";
}
?>
--EXPECT--
child: Cancellation of the iterator due to an exception
child cleanup done
caught: boom
