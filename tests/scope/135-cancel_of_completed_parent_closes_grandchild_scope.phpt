--TEST--
Scope: cancel() of a completed parent closes a held grandchild scope
--FILE--
<?php

use Async\Scope;
use function Async\await;

$parent = new Scope();
$child = Scope::inherit($parent);
$grandchild = Scope::inherit($child);
await($grandchild->spawn(fn() => null));

$parent->cancel();
echo "grandchild closed: ", var_export($grandchild->isClosed(), true), "\n";
?>
--EXPECT--
grandchild closed: true
