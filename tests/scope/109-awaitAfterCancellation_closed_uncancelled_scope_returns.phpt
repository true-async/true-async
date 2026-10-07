--TEST--
Scope: a cancel closes a scope whose only child scope is cancelled without cancelling it; awaitAfterCancellation() on it returns at once while the child's zombie runs, as TrueAsync's on a closed scope
--FILE--
<?php
use Async\Scope;
use function Async\spawn;
use function Async\await;
use function Async\delay;

$parent = Scope::inherit();
$child = Scope::inherit($parent)->allowZombies();
$child->spawn(function () {
    try {
        delay(1000);
    } catch (Async\AsyncCancellation) {
    }
    delay(50);
    echo "zombie done\n";
});
Async\suspend();
$child->cancel();
$parent->cancel();
printf("parent closed %d, cancelled %d\n", $parent->isClosed(), $parent->isCancelled());

await(spawn(function () use ($parent) {
    $parent->awaitAfterCancellation();
    echo "waiter returned\n";
}));
?>
--EXPECT--
parent closed 1, cancelled 0
waiter returned
zombie done
