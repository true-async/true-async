--TEST--
Scope: asNotSafely() - basic usage
--XFAIL--
Not implemented yet: S9.2 of dev/PLAN.md
--FILE--
<?php

use Async\Scope;

$scope = new Scope();
$notSafeScope = $scope->asNotSafely();

var_dump($notSafeScope === $scope);
var_dump($notSafeScope instanceof Scope);

?>
--EXPECT--
bool(true)
bool(true)