--TEST--
Scope: the walks over child scopes count every child, not only the first: isFinished() with a busy second child scope, awaitAfterCancellation() while a later child scope's zombie runs, and the error route cancelling every child scope
--FILE--
<?php
use Async\Scope;
use Async\Future;
use Async\FutureState;
use function Async\suspend;

echo "--- isFinished\n";
$gate = new FutureState();
$parent = new Scope();
$idle = Scope::inherit($parent);
$busy = Scope::inherit($parent);
$member = $busy->spawn(fn() => (new Future($gate))->await());
while ($member->getAwaitingInfo() === []) {
    suspend();
}
echo "busy second child scope: finished ", var_export($parent->isFinished(), true), "\n";
$gate->complete(1);
while (!$member->isCompleted()) {
    suspend();
}
echo "after its member: finished ", var_export($parent->isFinished(), true), "\n";

echo "--- awaitAfterCancellation\n";
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

echo "--- error route\n";
$outer = new Scope();
$outer->setChildScopeExceptionHandler(function (Scope $scope, Async\Coroutine $coroutine, Throwable $error) {
    echo "outer: took ", $error->getMessage(), "\n";
});
$failing = Scope::inherit($outer)->asNotSafely();
$log = [];
$members = [];
$child_scopes = [Scope::inherit($failing), Scope::inherit($failing)];
foreach (['first', 'second'] as $index => $name) {
    $members[] = $child_scopes[$index]->spawn(function () use ($name, &$log) {
        try {
            (new Future(new FutureState()))->await();
        } catch (Async\AsyncCancellation $e) {
            $log[] = "$name child scope's member cancelled";
        }
    });
}
while ($members[0]->getAwaitingInfo() === [] || $members[1]->getAwaitingInfo() === []) {
    suspend();
}
$failing->spawn(function () {
    throw new RuntimeException("failed");
});
while (!$members[0]->isCompleted() || !$members[1]->isCompleted()) {
    suspend();
}
sort($log);
echo implode("\n", $log), "\n";
?>
--EXPECT--
--- isFinished
busy second child scope: finished false
after its member: finished true
--- awaitAfterCancellation
early zombie: done
late zombie: done
waiter: returned
--- error route
outer: took failed
first child scope's member cancelled
second child scope's member cancelled
