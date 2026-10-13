--TEST--
Scope: awaitAfterCancellation() counts every child scope, not only the first: it waits while a later child scope's zombie runs
--FILE--
<?php
use Async\Scope;
use Async\Future;
use Async\FutureState;
use function Async\suspend;

$go = new FutureState();
$early_done = new FutureState();
$parent = (new Scope())->allowZombies();
// Closed by the cancel, not cancelled, and held: the parent is not disposed when its subtree empties.
$held = Scope::inherit($parent);
$late = Scope::inherit($parent);
$early_zombie = $parent->spawn(function () use ($go, $early_done) {
    (new Future($go))->await();
    echo "early zombie: done\n";
    $early_done->complete(1);
});
$late_zombie = $late->spawn(function () use ($early_done) {
    (new Future($early_done))->await();
    Async\delay(10);
    echo "late zombie: done\n";
});
while ($early_zombie->getAwaitingInfo() === [] || $late_zombie->getAwaitingInfo() === []) {
    suspend();
}
$parent->cancel();
$go->complete(1);
$parent->awaitAfterCancellation();
echo "waiter: returned\n";
?>
--EXPECT--
early zombie: done
late zombie: done
waiter: returned
