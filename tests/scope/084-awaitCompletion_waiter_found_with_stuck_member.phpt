--TEST--
get_deadlocked_coroutines(): a waiter in awaitCompletion() is found with the scope's stuck member when only the waiter holds the Scope object
--FILE--
<?php
use Async\Scope;
use Async\Future;
use Async\FutureState;
use function Async\await;
use function Async\spawn;
use function Async\delay;
use function Async\get_deadlocked_coroutines;

$scope = new Scope();
$memberId = $scope->spawn(function () {
    try {
        await(new Future(new FutureState()));
    } catch (Async\AsyncCancellation $e) {
        echo "member: ", $e->getMessage(), "\n";
    }
})->getId();
$waiterId = spawn(function () use ($scope) {
    try {
        $scope->awaitCompletion(new Future(new FutureState()));
        echo "waiter: completed\n";
    } catch (Async\AsyncCancellation $e) {
        echo "waiter: ", $e->getMessage(), "\n";
    }
})->getId();
unset($scope);

delay(10);
$found = get_deadlocked_coroutines();
foreach ($found as $coroutine) {
    echo "found: ", $coroutine->getId() === $memberId ? "member" : ($coroutine->getId() === $waiterId ? "waiter" : "other"), "\n";
}
foreach ($found as $coroutine) {
    $coroutine->cancel();
}
unset($found, $coroutine);
delay(10);
echo "end\n";
?>
--EXPECT--
found: member
found: waiter
member: Coroutine cancelled
waiter: Coroutine cancelled
end
