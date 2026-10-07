--TEST--
Scope: isFinished() and isClosed() - basic usage
--XFAIL--
Not implemented yet: S9.2 of dev/PLAN.md
--FILE--
<?php

use Async\Scope;

$scope = new Scope();

var_dump($scope->isFinished());
var_dump($scope->isClosed());

$scope->cancel();

var_dump($scope->isClosed());

?>
--EXPECT--
bool(true)
bool(false)
bool(true)