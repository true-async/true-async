--TEST--
Scope: with safe disposal, a member's unhandled error does not reach a waiter in awaitCompletion() and goes on to the parent
--DESCRIPTION--
As TrueAsync: the zombie mark of the cascade wakes the waiter first (dev/plans/S9-scope.md 4, step 2;
probe s9.4/w2.php). The end prints the unobserved error of the held coroutine (S3).
--FILE--
<?php

use Async\Scope;
use function Async\spawn;
use function Async\delay;
use function Async\await;
use function Async\timeout;

$parent = Scope::inherit();
$scope = Scope::inherit($parent);

$started = 0;
$waiting = false;
$waiter_ended = false;
$sibling_woke = false;

$sibling = $parent->spawn(function () {
    $GLOBALS['started']++;

    try {
        // Fixed delays could end before the error reaches the waiter, or in either order
        while (!$GLOBALS['waiter_ended']) {
            delay(1);
        }

        echo "parent sibling woke\n";
        $GLOBALS['sibling_woke'] = true;
    } catch (Throwable $e) {
        echo "parent sibling: ", $e->getMessage(), "\n";
    }
});
$member = $scope->spawn(function () {
    $GLOBALS['started']++;

    try {
        while (!$GLOBALS['sibling_woke']) {
            delay(1);
        }

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
await($member);
echo "scope cancelled: ", var_export($scope->isCancelled(), true), "\n";
echo "parent cancelled: ", var_export($parent->isCancelled(), true), "\n";
echo "end\n";

?>
--EXPECTF--
waiter returned
parent sibling woke
member woke
scope cancelled: true
parent cancelled: true
end

Fatal error: Uncaught RuntimeException: boom in %s:%d
Stack trace:
#0 [internal function]: {closure:%s}()
#1 {main}
  thrown in %s on line %d
