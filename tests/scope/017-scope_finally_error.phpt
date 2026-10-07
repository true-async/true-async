--TEST--
Scope finally single exception handling
--XFAIL--
Not implemented yet: S9.6 of dev/PLAN.md
--FILE--
<?php

use Async\Scope;

$scope = new Scope();

$scope->setExceptionHandler(function($scope, $coroutine, $exception) {
    echo "Caught single exception: " . $exception->getMessage() . "\n";
});

$coro = $scope->spawn(function() {
    return "result";
});

// Add single finally handler that will throw exception
$scope->finally(function() {
    throw new Exception("Single exception");
});

$scope->dispose();

?>
--EXPECT--
Caught single exception: Single exception