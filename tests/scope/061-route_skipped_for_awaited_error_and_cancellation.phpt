--TEST--
Scope: an error a waiter was parked for, and a cancellation, end at their coroutine: no handler, no cancel of the scope
--FILE--
<?php

use Async\Scope;
use function Async\spawn;
use function Async\await;
use function Async\delay;

// p7.php of dev/plans/S9-scope.md 4: main waits in await($a) when $a throws, so $b, queued after it,
// still runs.
$a = spawn(fn() => throw new RuntimeException("awaited"));
$b = spawn(function () {
    echo "b ran\n";
});

try {
    await($a);
} catch (RuntimeException $e) {
    echo "caught: ", $e->getMessage(), "\n";
}

echo "b cancelled: ", var_export($b->isCancelled(), true), "\n";

$scope = Scope::inherit();
$scope->setExceptionHandler(function (Scope $scope, Async\Coroutine $coroutine, Throwable $e) {
    echo "handler ran: ", get_class($e), "\n";
});

$sleeper = $scope->spawn(fn() => delay(100));
delay(1);
$sleeper->cancel();
delay(10);

$thrower = $scope->spawn(function () {
    delay(5);
    throw new RuntimeException("awaited in scope");
});

try {
    await($thrower);
} catch (RuntimeException $e) {
    echo "caught: ", $e->getMessage(), "\n";
}

echo "scope cancelled: ", var_export($scope->isCancelled(), true), "\n";

?>
--EXPECT--
b ran
caught: awaited
b cancelled: false
caught: awaited in scope
scope cancelled: false
