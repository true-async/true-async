--TEST--
iterate(): a spawned coroutine's error stops the walk when the callback catches the cancellation
--FILE--
<?php

use function Async\delay;
use function Async\iterate;
use function Async\spawn;

try {
    iterate([1, 2, 3], function ($value) {
        echo "begin $value\n";
        if ($value === 1) {
            spawn(function () {
                throw new RuntimeException('child failed');
            });
        }
        try {
            delay(10000);
        } catch (Async\AsyncCancellation $cancellation) {
            echo "callback $value: ", $cancellation->getMessage(), "\n";
        }
    }, concurrency: 1, cancelPending: false);
    echo "iterate returned\n";
} catch (RuntimeException $exception) {
    echo "caught: ", $exception->getMessage(), "\n";
}
?>
--EXPECTF--
begin 1
callback 1: %s
caught: child failed
