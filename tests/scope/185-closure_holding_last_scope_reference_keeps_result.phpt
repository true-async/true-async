--TEST--
Scope: a coroutine whose closure holds the last reference to its Scope keeps its result when the closure's release cancels the scope
--FILE--
<?php

use Async\Scope;
use function Async\await;

$scope = new Scope();
$coroutine = $scope->spawn(function () use ($scope) {
    return 42;
});
unset($scope);

var_dump(await($coroutine));
var_dump($coroutine->isCancelled());
?>
--EXPECT--
int(42)
bool(false)
