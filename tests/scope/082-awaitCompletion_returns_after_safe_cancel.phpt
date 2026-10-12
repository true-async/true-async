--TEST--
Scope: with safe disposal, awaitCompletion() returns when cancel() makes the started coroutines zombies
--DESCRIPTION--
As TrueAsync: no active coroutine is left once they are zombies, and that wake comes before the
cancellation's (dev/plans/S9-scope.md 4, step 2; probe s9.4/w1.php).
--FILE--
<?php

use Async\Scope;
use function Async\spawn;
use function Async\delay;
use function Async\await;
use function Async\timeout;

$parked = 0;
$scope = Scope::inherit();
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
waiter returned
end
