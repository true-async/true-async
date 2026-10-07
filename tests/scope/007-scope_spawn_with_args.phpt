--TEST--
Scope: spawn() - with arguments
--XFAIL--
Not implemented yet: S9.2 of dev/PLAN.md
--FILE--
<?php

use Async\Scope;

$scope = new Scope();

$coroutine = $scope->spawn(function($a, $b) {
    return $a + $b;
}, 10, 20);

var_dump($coroutine instanceof Async\Coroutine);

?>
--EXPECT--
bool(true)