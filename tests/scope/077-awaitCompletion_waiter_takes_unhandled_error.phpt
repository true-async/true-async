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
--EXPECT--
member: Async\AsyncCancellation: Coroutine cancelled
waiter: RuntimeException: boom
parent sibling woke
scope cancelled: true
parent cancelled: false
end
