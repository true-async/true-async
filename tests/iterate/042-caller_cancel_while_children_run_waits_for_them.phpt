--TEST--
iterate(): a cancel of the caller while spawned coroutines run cancels them and waits for them
--FILE--
<?php

use function Async\await;
use function Async\delay;
use function Async\iterate;
use function Async\spawn;
use function Async\suspend;

$parked = false;

$caller = spawn(function () use (&$parked) {
    try {
        iterate([1], function () use (&$parked) {
            spawn(function () use (&$parked) {
                $parked = true;
                try {
                    delay(10000);
                } catch (Async\AsyncCancellation $cancellation) {
                    echo "child: ", $cancellation->getMessage(), "\n";
                    suspend();
                    echo "child ends\n";
                }
            });
        }, cancelPending: false);
        echo "iterate returned\n";
    } catch (Async\AsyncCancellation $cancellation) {
        echo "caller: ", $cancellation->getMessage(), "\n";
    }
});

while (!$parked) {
    suspend();
}

$caller->cancel(new Async\AsyncCancellation('stop'));
await($caller);
echo "end\n";
?>
--EXPECT--
child: stop
child ends
caller: stop
end
