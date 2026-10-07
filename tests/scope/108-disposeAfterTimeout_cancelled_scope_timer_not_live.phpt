--TEST--
get_deadlocked_coroutines(): a disposeAfterTimeout() timer armed on a cancelled scope, whose fire would only close it, does not keep its stuck zombie and waiter from being found
--FILE--
<?php
use Async\Scope;
use Async\Future;
use Async\FutureState;
use function Async\await;
use function Async\spawn;
use function Async\delay;
use function Async\get_deadlocked_coroutines;

$scope = (new Scope())->allowZombies();
$memberId = $scope->spawn(function () {
    try {
        await(new Future(new FutureState()));
    } catch (Async\AsyncCancellation $e) {
        echo "member: ", $e->getMessage(), "\n";
    }
})->getId();
Async\suspend();
$scope->cancel();
$scope->disposeAfterTimeout(5000);
$waiterId = spawn(function () use ($scope) {
    try {
        $scope->awaitAfterCancellation();
        echo "waiter: completed\n";
    } catch (Async\AsyncCancellation $e) {
        echo "waiter: ", $e->getMessage(), "\n";
    }
})->getId();
unset($scope);

Async\suspend();
delay(10);
$found = get_deadlocked_coroutines();
foreach ($found as $coroutine) {
    echo "found: ", $coroutine->getId() === $memberId ? "member" : ($coroutine->getId() === $waiterId ? "waiter" : "other"), "\n";
}
foreach ($found as $coroutine) {
    $coroutine->cancel();
}
unset($found, $coroutine);
Async\suspend();
delay(10);
echo "end\n";
?>
--EXPECT--
found: member
found: waiter
member: Coroutine cancelled
waiter: Coroutine cancelled
end
