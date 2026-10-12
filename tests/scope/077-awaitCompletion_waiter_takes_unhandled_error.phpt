--TEST--
Scope: a waiter in awaitCompletion() takes a member's unhandled error, which then stops at the scope and leaves the parent running
--FILE--
<?php

use Async\Scope;
use function Async\spawn;
use function Async\delay;
use function Async\await;
use function Async\timeout;

$parent = Scope::inherit()->asNotSafely();
$scope = Scope::inherit($parent);

$started = 0;
$waiting = false;
$waiter_ended = false;

$sibling = $parent->spawn(function () {
    $GLOBALS['started']++;

    try {
        // A fixed delay could end before the error reaches the waiter
        while (!$GLOBALS['waiter_ended']) {
            delay(1);
        }

        echo "parent sibling woke\n";
    } catch (Throwable $e) {
        echo "parent sibling: ", $e->getMessage(), "\n";
    }
});
$member = $scope->spawn(function () {
    $GLOBALS['started']++;

    try {
        // The route cancels this wait; a short one could end first under load
        delay(100000);
        echo "member woke\n";
    } catch (Throwable $e) {
        echo "member: ", get_class($e), ": ", $e->getMessage(), "\n";
    }
});
$failing = $scope->spawn(function () {
    // The waiter must be in awaitCompletion() and the others started first, or the cascade cancels them
    // unstarted; each parks right after its mark
    while (!$GLOBALS['waiting'] || $GLOBALS['started'] < 2) {
        Async\suspend();
    }

    throw new RuntimeException("boom");
});

$waiter = spawn(function () use ($scope) {
    try {
        $GLOBALS['waiting'] = true;
        $scope->awaitCompletion(timeout(2000));
        echo "waiter returned\n";
    } catch (Throwable $e) {
        echo "waiter: ", get_class($e), ": ", $e->getMessage(), "\n";
    }

    $GLOBALS['waiter_ended'] = true;
});

await($waiter);
await($sibling);
echo "scope cancelled: ", var_export($scope->isCancelled(), true), "\n";
echo "parent cancelled: ", var_export($parent->isCancelled(), true), "\n";
echo "end\n";

?>
--EXPECT--
member: Async\AsyncCancellation: Coroutine cancelled
waiter: RuntimeException: boom
parent sibling woke
scope cancelled: true
parent cancelled: false
end
