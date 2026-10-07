--TEST--
The iterator core's error goes up the route when the last worker to leave was cancelled before it ran
--FILE--
<?php

use Async\Coroutine;
use Async\Scope;
use function TrueAsync\Test\iterate;

$scope = new Scope();
$scope->setChildScopeExceptionHandler(function (Scope $scope, Coroutine $coroutine, Throwable $error) {
    echo "caught: ", $error->getMessage(), "\n";
});
$scope->spawn(function () {
    iterate([1, 2], function ($value) {
        if ($value === 1) {
            Async\suspend();
            echo "first throws\n";
            throw new Exception('boom');
        }
        echo "not reached\n";
    });
});
$scope->awaitCompletion(Async\timeout(1000));
echo "end\n";

?>
--EXPECT--
first throws
caught: boom
end
