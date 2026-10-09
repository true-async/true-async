--TEST--
Scope: cancel() of a completed parent closes a child scope the script still holds
--FILE--
<?php

use Async\Scope;
use function Async\await;

$parent = new Scope();
$child = Scope::inherit($parent);
await($child->spawn(fn() => null));

$parent->cancel();
echo "parent closed: ", var_export($parent->isClosed(), true), "\n";
echo "child closed: ", var_export($child->isClosed(), true), "\n";
?>
--EXPECT--
parent closed: true
child closed: true
