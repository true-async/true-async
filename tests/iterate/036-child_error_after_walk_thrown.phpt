--TEST--
iterate(): a spawned coroutine's error after the walk is thrown by iterate()
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
                suspend();
                throw new RuntimeException('child failed');
            });
        }, cancelPending: false);
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
caught: child failed
caller goes on
end
