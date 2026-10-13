--TEST--
iterate(): a spawned coroutine's error right after the last callback is thrown by iterate()
--FILE--
<?php

use function Async\await;
use function Async\iterate;
use function Async\spawn;
use function Async\suspend;

$caller = spawn(function () {
    try {
        iterate([1], function () {
            spawn(function () {
                try {
                    suspend();
                    suspend();
                } catch (Async\AsyncCancellation $cancellation) {
                    echo "sibling: ", $cancellation->getMessage(), "\n";
                }
            });
            spawn(function () {
                throw new RuntimeException('child failed');
            });
        });
        echo "iterate returned\n";
    } catch (RuntimeException $exception) {
        echo "caught: ", $exception->getMessage(), "\n";
    }
    echo "caller goes on\n";
});

await($caller);
echo "end\n";
?>
--EXPECT--
sibling: Coroutine cancelled
caught: child failed
caller goes on
end
