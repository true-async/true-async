--TEST--
get_deadlocked_coroutines(): a found waiter in awaitCompletion() takes the error of a found member that was cancelled and threw
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
        throw new RuntimeException("member failed");
    }
})->getId();
spawn(function () use ($scope) {
    try {
        $scope->awaitCompletion(new Future(new FutureState()));
        echo "waiter: completed\n";
    } catch (Throwable $e) {
        echo "waiter: ", get_class($e), ": ", $e->getMessage(), "\n";
    }
});
unset($scope);

delay(10);
$found = get_deadlocked_coroutines();
echo "found: ", count($found), "\n";
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
found: 2
waiter: RuntimeException: member failed
end
