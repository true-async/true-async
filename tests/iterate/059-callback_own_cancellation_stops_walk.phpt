--TEST--
iterate(): a cancellation a callback throws itself, its coroutine not cancelled, stops the walk and is thrown
--FILE--
<?php

use function Async\await;
use function Async\delay;
use function Async\iterate;
use function Async\spawn;
use function Async\timeout;

try {
    iterate([1, 2, 3], function ($value) {
        echo "begin $value\n";
        if ($value === 2) {
            await(spawn(fn () => delay(10000)), timeout(10));
        }
        echo "end $value\n";
    }, concurrency: 1);
    echo "iterate returned\n";
} catch (Async\OperationCanceledException $exception) {
    echo "caught: ", get_class($exception), "\n";
}
?>
--EXPECT--
begin 1
end 1
begin 2
caught: Async\OperationCanceledException
