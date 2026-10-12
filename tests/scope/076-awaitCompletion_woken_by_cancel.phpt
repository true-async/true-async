--TEST--
Scope: awaitCompletion() throws the cancellation that cancel() gives the scope's coroutines
--FILE--
<?php

use Async\Scope;
use function Async\spawn;
use function Async\delay;
use function Async\await;
use function Async\timeout;

$parked = 0;
$scope = Scope::inherit()->asNotSafely();
$scope->spawn(function () {
    try {
        $GLOBALS['parked']++;
        delay(1000);
    } catch (Throwable $e) {
        echo "member: ", $e->getMessage(), "\n";
    }
});

$waiter = spawn(function () use ($scope) {
    try {
        $GLOBALS['parked']++;
        $scope->awaitCompletion(timeout(2000));
        echo "waiter returned\n";
    } catch (Throwable $e) {
        echo "waiter: ", get_class($e), ": ", $e->getMessage(), "\n";
    }
});

// A coroutine runs on to its park, so the count means they have parked. A delay instead could end
// first: a timer due when main parks wakes it in place, ahead of the queue.
while ($parked < 2) {
    Async\suspend();
}

$scope->cancel();
await($waiter);
echo "end\n";

?>
--EXPECT--
member: Scope was cancelled
waiter: Async\AsyncCancellation: Scope was cancelled
end
