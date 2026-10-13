--TEST--
iterate(): a cancellation a callback throws itself cancels the other callbacks, as an exception does
--FILE--
<?php

use function Async\delay;
use function Async\iterate;

try {
    iterate([1, 2], function ($value) {
        if ($value === 2) {
            throw new Async\AsyncCancellation('own');
        }
        try {
            delay(10000);
        } catch (Async\AsyncCancellation $cancellation) {
            echo "callback $value: ", $cancellation->getMessage(), "\n";
        }
    });
    echo "iterate returned\n";
} catch (Async\AsyncCancellation $cancellation) {
    echo "caught: ", $cancellation->getMessage(), "\n";
}
?>
--EXPECT--
callback 1: Cancellation of the iterator due to an exception
caught: own
