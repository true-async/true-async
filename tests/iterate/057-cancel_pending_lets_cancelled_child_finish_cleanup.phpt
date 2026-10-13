--TEST--
iterate(): after a callback's exception, cancelPending does not cancel again a child that handles the first cancel
--FILE--
<?php

use function Async\delay;
use function Async\iterate;
use function Async\spawn;

try {
    iterate([1, 2], function ($value) {
        if ($value === 1) {
            spawn(function () {
                try {
                    delay(10000);
                } catch (Async\AsyncCancellation $cancellation) {
                    echo "child: ", $cancellation->getMessage(), "\n";
                    delay(20);
                    echo "child cleanup done\n";
                }
            });
            delay(5);
            throw new Exception('boom');
        }
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
