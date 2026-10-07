--TEST--
Scope: awaitCompletion() throws the cancellation that cancel() gives the scope's coroutines
--FILE--
<?php

use Async\Scope;
use function Async\spawn;
use function Async\delay;
use function Async\await;
use function Async\timeout;

$scope = Scope::inherit()->asNotSafely();
$scope->spawn(function () {
    try {
        delay(1000);
    } catch (Throwable $e) {
        echo "member: ", $e->getMessage(), "\n";
    }
});

$waiter = spawn(function () use ($scope) {
    try {
        $scope->awaitCompletion(timeout(2000));
        echo "waiter returned\n";
    } catch (Throwable $e) {
        echo "waiter: ", get_class($e), ": ", $e->getMessage(), "\n";
    }
});

delay(10);
$scope->cancel();
await($waiter);
echo "end\n";

?>
--EXPECT--
member: Scope was cancelled
waiter: Async\AsyncCancellation: Scope was cancelled
end
