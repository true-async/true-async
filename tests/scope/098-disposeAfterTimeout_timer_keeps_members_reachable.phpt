--TEST--
get_deadlocked_coroutines(): a scope's disposeAfterTimeout() timer can cancel its members, so neither they nor a waiter in awaitCompletion() are found before it fires
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
$scope->spawn(function () {
    try {
        await(new Future(new FutureState()));
    } catch (Async\AsyncCancellation $e) {
        echo "member: ", $e->getMessage(), "\n";
    }
});
$scope->disposeAfterTimeout(100);
spawn(function () use ($scope) {
    try {
        $scope->awaitCompletion(new Future(new FutureState()));
        echo "waiter: completed\n";
    } catch (Async\AsyncCancellation $e) {
        echo "waiter: ", $e->getMessage(), "\n";
    }
});
unset($scope);

Async\suspend();
delay(10);
echo "found: ", count(get_deadlocked_coroutines()), "\n";
delay(200);
echo "end\n";
?>
--EXPECT--
found: 0
member: Scope has been disposed due to timeout
waiter: Scope has been disposed due to timeout
end
