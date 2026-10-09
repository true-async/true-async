--TEST--
Scope: cancel() of a completed parent closes every child scope the script still holds
--FILE--
<?php

use Async\Scope;

$parent = new Scope();
$first = Scope::inherit($parent);
$second = Scope::inherit($parent);

$parent->cancel();
echo "first closed: ", var_export($first->isClosed(), true), "\n";
echo "second closed: ", var_export($second->isClosed(), true), "\n";
?>
--EXPECT--
first closed: true
second closed: true
