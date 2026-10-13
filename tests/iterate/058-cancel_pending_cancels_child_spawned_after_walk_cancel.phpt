--TEST--
iterate(): cancelPending cancels a coroutine spawned into the iterator's scope after the walk's exception cancelled it
--FILE--
<?php

use function Async\delay;
use function Async\iterate;
use function Async\spawn;

try {
    iterate([1, 2], function ($value) {
        if ($value === 1) {
            delay(5);
            throw new Exception('boom');
        }
        try {
            delay(10000);
        } catch (Async\AsyncCancellation) {
        }
        spawn(function () {
            try {
                delay(10000);
                echo "child finished\n";
            } catch (Async\AsyncCancellation $cancellation) {
                echo "child: ", $cancellation->getMessage(), "\n";
            }
        });
    });
} catch (Exception $exception) {
    echo "caught: ", $exception->getMessage(), "\n";
}
?>
--EXPECT--
child: Cancellation of pending coroutines after iterator completion
caught: boom
