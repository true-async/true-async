--TEST--
iterate(): an awaiter's timeout leaves the walk running
--FILE--
<?php

use function Async\await;
use function Async\iterate;
use function Async\spawn;
use function Async\suspend;
use function Async\timeout;

$parked = false;
$release = false;

$caller = spawn(function () use (&$parked, &$release) {
    iterate([1, 2], function ($value) use (&$parked, &$release) {
        $parked = true;
        while (!$release) {
            suspend();
        }
        echo "callback $value finished\n";
    }, concurrency: 1);
    echo "iterate returned\n";
});

while (!$parked) {
    suspend();
}

try {
    await($caller, timeout(1));
} catch (Async\OperationCanceledException $exception) {
    echo "await: ", get_class($exception), "\n";
}

$release = true;
await($caller);
echo "end\n";
?>
--EXPECT--
await: Async\OperationCanceledException
callback 1 finished
callback 2 finished
iterate returned
end
