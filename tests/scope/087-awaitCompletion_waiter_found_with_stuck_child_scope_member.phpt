--TEST--
get_deadlocked_coroutines(): a waiter in awaitCompletion() is found with a stuck member of a child scope, and the member's end still wakes it
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
$child = Scope::inherit($scope);
$memberId = $child->spawn(function () {
    try {
        await(new Future(new FutureState()));
    } catch (Async\AsyncCancellation $e) {
        echo "member: ", $e->getMessage(), "\n";
    }
})->getId();
$waiterId = spawn(function () use ($scope, $child) {
    $scope->awaitCompletion(new Future(new FutureState()));
    echo "waiter: completed\n";
})->getId();
unset($scope, $child);

delay(10);
$found = get_deadlocked_coroutines();
foreach ($found as $coroutine) {
    echo "found: ", $coroutine->getId() === $memberId ? "member" : ($coroutine->getId() === $waiterId ? "waiter" : "other"), "\n";
}
// Only the member: its end completes the scope, which wakes the waiter the run found.
foreach ($found as $coroutine) {
    if ($coroutine->getId() === $memberId) {
        $coroutine->cancel();
    }
}
unset($found, $coroutine);
delay(10);
echo "end\n";
?>
--EXPECT--
found: member
found: waiter
member: Coroutine cancelled
waiter: completed
end
