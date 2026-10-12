--TEST--
Scope: a second setExceptionHandler() replaces the first, which is never called for a later error
--DESCRIPTION--
The case scope/043 leaves unchecked: it only sets the handlers.
--FILE--
<?php

use Async\Coroutine;
use Async\Scope;

$scope = new Scope();
$scope->setExceptionHandler(function () {
    echo "first handler\n";
});
$scope->setExceptionHandler(function (Scope $scope, Coroutine $coroutine, Throwable $error) {
    echo "second handler: ", $error->getMessage(), "\n";
});

$scope->spawn(function () {
    throw new RuntimeException("member failed");
});
Async\suspend();
?>
--EXPECT--
second handler: member failed
