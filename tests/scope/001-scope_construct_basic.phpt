--TEST--
Scope: __construct() - basic usage
--XFAIL--
Not implemented yet: S9.2 of dev/PLAN.md
--FILE--
<?php

use Async\Scope;

$scope = new Scope();
var_dump($scope instanceof Scope);
var_dump($scope instanceof Async\ScopeProvider);

?>
--EXPECT--
bool(true)
bool(true)