--TEST--
Scope: dispose() of a completed parent closes a child scope the script still holds
--FILE--
<?php

use Async\Scope;
use function Async\await;

$parent = new Scope();
$child = Scope::inherit($parent);
await($child->spawn(fn() => null));

$parent->dispose();
echo "child closed: ", var_export($child->isClosed(), true), "\n";
?>
--EXPECT--
child closed: true
