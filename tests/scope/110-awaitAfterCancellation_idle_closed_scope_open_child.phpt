--TEST--
Scope: dispose() of an idle scope closes it and its child scope; awaitAfterCancellation() on it returns at once, since the scope is not cancelled
--FILE--
<?php
use Async\Scope;
use function Async\spawn;
use function Async\await;

$parent = Scope::inherit();
$child = Scope::inherit($parent);
$parent->dispose();
printf("parent closed %d, cancelled %d; child closed %d, cancelled %d\n",
    $parent->isClosed(), $parent->isCancelled(), $child->isClosed(), $child->isCancelled());

await(spawn(function () use ($parent) {
    $parent->awaitAfterCancellation();
    echo "waiter returned\n";
}));
?>
--EXPECT--
parent closed 1, cancelled 0; child closed 1, cancelled 0
waiter returned
