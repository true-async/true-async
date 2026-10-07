--TEST--
Scope: an error of an unheld coroutine that a handler takes ends neither the request nor the scope
--FILE--
<?php

use Async\Scope;
use function Async\delay;

$scope = Scope::inherit()->asNotSafely();
$scope->setExceptionHandler(function (Scope $scope, Async\Coroutine $coroutine, Throwable $e) {
    echo "handled: ", $e->getMessage(), "\n";
});

$sibling = $scope->spawn(function () {
    delay(20);
    echo "sibling woke\n";
});
$scope->spawn(function () {
    throw new RuntimeException("unheld");
});

delay(50);
echo "scope cancelled: ", var_export($scope->isCancelled(), true), "\n";
echo "end\n";

?>
--EXPECT--
handled: unheld
sibling woke
scope cancelled: false
end
