--TEST--
Scope: a cancel closes an idle scope without touching its child scope; awaitAfterCancellation() on it returns at once, since the scope is not cancelled, while that child's coroutine runs
--FILE--
<?php
use Async\Scope;
use function Async\spawn;
use function Async\await;
use function Async\delay;

$parent = Scope::inherit();
$child = Scope::inherit($parent);
$parent->dispose();
printf("parent closed %d, cancelled %d; child closed %d, cancelled %d\n",
    $parent->isClosed(), $parent->isCancelled(), $child->isClosed(), $child->isCancelled());

$child->spawn(function () {
    delay(100);
    echo "child member done\n";
});

await(spawn(function () use ($parent) {
    $parent->awaitAfterCancellation();
    echo "waiter returned\n";
}));
?>
--EXPECT--
parent closed 1, cancelled 0; child closed 0, cancelled 0
waiter returned
child member done
