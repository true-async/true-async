--TEST--
Scope: awaitCompletion() woken by its last member's end waits again for a member spawned before it ran
--FILE--
<?php
use Async\Scope;
use Async\Future;
use Async\FutureState;
use function Async\spawn;
use function Async\await;
use function Async\suspend;

$scope = new Scope();
$gate = new FutureState();
$first = $scope->spawn(function () use ($gate) {
    await(new Future($gate));
});
$waiter = spawn(function () use ($scope) {
    $scope->awaitCompletion(Async\timeout(1000));
    echo "finished: ", var_export($scope->isFinished(), true), "\n";
});
while (!$first->isStarted() || !$waiter->isStarted()) {
    suspend();
}

$gate->complete(null);
suspend();
$scope->spawn(function () {
    suspend();
});
await($waiter);
echo "end\n";
?>
--EXPECT--
finished: true
end
