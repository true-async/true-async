--TEST--
iterate(): cancelPending cancels a coroutine spawned into the iterator's scope after the walk's exception cancelled it
--FILE--
<?php

use function Async\delay;
use function Async\iterate;
use function Async\spawn;
use function Async\suspend;

$parked = false;

try {
    iterate([1, 2], function ($value) use (&$parked) {
        if ($value === 1) {
            // The second worker parks before the walk's exception cancels it.
            while (!$parked) {
                suspend();
            }

            throw new Exception('boom');
        }

        try {
            $parked = true;
            delay(10000);
        } catch (Async\AsyncCancellation) {
        }

        $child_parked = false;

        spawn(function () use (&$child_parked) {
            try {
                $child_parked = true;
                delay(10000);
                echo "child finished\n";
            } catch (Async\AsyncCancellation $cancellation) {
                echo "child: ", $cancellation->getMessage(), "\n";
            }
        });

        // The walk ends with this worker: cancelPending finds the child parked, not unstarted.
        while (!$child_parked) {
            suspend();
        }
    });
} catch (Exception $exception) {
    echo "caught: ", $exception->getMessage(), "\n";
}
?>
--EXPECT--
child: Cancellation of pending coroutines after iterator completion
caught: boom
