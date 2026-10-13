--TEST--
Scope: setExceptionHandler() and setChildScopeExceptionHandler() - basic usage
--FILE--
<?php

use Async\Scope;
use function Async\suspend;

$scope = new Scope();

$scope->setExceptionHandler(function(Scope $scope, Async\Coroutine $coroutine, Throwable $exception) {
    echo "Exception handled: " . $exception->getMessage() . "\n";
});

$scope->setChildScopeExceptionHandler(function(Scope $scope, Async\Coroutine $coroutine, Throwable $exception) {
    echo "Child scope exception handled: " . $exception->getMessage() . "\n";
});

echo "Exception handlers set successfully\n";

$own = $scope->spawn(function() {
    throw new RuntimeException("own");
});

$child = Scope::inherit($scope);
$member = $child->spawn(function() {
    throw new RuntimeException("child");
});

while (!$own->isCompleted() || !$member->isCompleted()) {
    suspend();
}

?>
--EXPECT--
Exception handlers set successfully
Exception handled: own
Child scope exception handled: child