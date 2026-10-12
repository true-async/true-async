--TEST--
Scope: what a Traversable spawns while await_all() walks it belongs to a child scope of the caller's, which the caller's cancel() reaches
--FILE--
<?php

use Async\Scope;
use function Async\spawn;
use function Async\delay;
use function Async\await_all;

function items(): Generator
{
    yield spawn(function () {
        try {
            $GLOBALS['parked']++;
            delay(1000);
            echo "item woke\n";
        } catch (Throwable $e) {
            echo "item: ", $e->getMessage(), "\n";
        }

        $GLOBALS['ended']++;
    });
}

$parked = 0;
$ended = 0;
$scope = Scope::inherit()->asNotSafely();
$caller = $scope->spawn(function () {
    try {
        await_all(items());
    } catch (Throwable $e) {
        echo "caller: ", get_class($e), ": ", $e->getMessage(), "\n";
    }

    $GLOBALS['ended']++;
});

// A coroutine runs on to its park, so the count means they have parked. A delay instead could end
// first: a timer due when main parks wakes it in place, ahead of the queue.
while ($parked < 1) {
    Async\suspend();
}

$scope->cancel();

while ($ended < 2) {
    Async\suspend();
}

echo "end\n";

?>
--EXPECT--
item: Scope was cancelled
caller: Async\AsyncCancellation: Scope was cancelled
end
