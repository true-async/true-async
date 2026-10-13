--TEST--
iterate(): a child error while the cancelled caller waits in the queue stops at the iterator's scope
--FILE--
<?php

use function Async\await;
use function Async\delay;
use function Async\iterate;
use function Async\spawn;
use function Async\suspend;

$parked = false;

$caller = spawn(function () {
    try {
        iterate([1], function () {
            spawn(function () {
                global $parked;
                $parked = true;
                suspend();
                throw new RuntimeException('child error');
            });
            delay(1000);
        });
    } catch (Throwable $e) {
        echo get_class($e), ": ", $e->getMessage(), "\n";
        echo "previous: ", get_debug_type($e->getPrevious()), ": ", $e->getPrevious()?->getMessage(), "\n";
    }
});

while (!$parked) {
    suspend();
}

// The caller is queued with its cancellation; the child, queued ahead of it, throws.
$caller->cancel();
await($caller);
echo "main goes on\n";
?>
--EXPECT--
Async\AsyncCancellation: Coroutine cancelled
previous: RuntimeException: child error
main goes on
