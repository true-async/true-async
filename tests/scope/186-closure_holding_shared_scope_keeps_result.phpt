--TEST--
Scope: a coroutine whose closure holds a Scope that is also held elsewhere keeps its result
--FILE--
<?php

use Async\Scope;
use function Async\await;

$scope = new Scope();
$coroutine = $scope->spawn(function () use ($scope) {
    return 42;
});

var_dump(await($coroutine));
var_dump($coroutine->isCancelled());
?>
--EXPECT--
int(42)
bool(false)
