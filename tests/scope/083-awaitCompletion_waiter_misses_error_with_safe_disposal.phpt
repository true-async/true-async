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

$sibling = $parent->spawn(function () {
    try {
        delay(100);
        echo "parent sibling woke\n";
    } catch (Throwable $e) {
        echo "parent sibling: ", $e->getMessage(), "\n";
    }
});
$member = $scope->spawn(function () {
    try {
        delay(100);
        echo "member woke\n";
    } catch (Throwable $e) {
        echo "member: ", get_class($e), ": ", $e->getMessage(), "\n";
    }
});
$failing = $scope->spawn(function () {
    delay(10);
    throw new RuntimeException("boom");
});

$waiter = spawn(function () use ($scope) {
    try {
        $scope->awaitCompletion(timeout(2000));
        echo "waiter returned\n";
    } catch (Throwable $e) {
        echo "waiter: ", get_class($e), ": ", $e->getMessage(), "\n";
    }
});

await($waiter);
delay(200);
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
