--TEST--
Scope: an error of an unheld coroutine that a handler takes ends neither the request nor the scope
--FILE--
<?php

use Async\Scope;
use function Async\await;
use function Async\delay;

$handled = false;
$scope = Scope::inherit()->asNotSafely();
$scope->setExceptionHandler(function (Scope $scope, Async\Coroutine $coroutine, Throwable $e) {
    echo "handled: ", $e->getMessage(), "\n";
    $GLOBALS['handled'] = true;
});

$sibling = $scope->spawn(function () {
    // Parked on a timer until the error is handled: one delay could end before the thrower runs
    while (!$GLOBALS['handled']) {
        delay(1);
    }

    echo "sibling woke\n";
});
$scope->spawn(function () {
    throw new RuntimeException("unheld");
});

await($sibling);
echo "scope cancelled: ", var_export($scope->isCancelled(), true), "\n";
echo "end\n";

?>
--EXPECT--
handled: unheld
sibling woke
scope cancelled: false
end
