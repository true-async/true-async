--TEST--
get_deadlocked_coroutines(): neither the stuck member nor the waiter in awaitCompletion() is found while main holds the Scope object
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
spawn(function () use ($scope) {
    try {
        $scope->awaitCompletion(new Future(new FutureState()));
        echo "waiter: completed\n";
    } catch (Async\AsyncCancellation $e) {
        echo "waiter: ", $e->getMessage(), "\n";
    }
});

delay(10);
echo "found: ", count(get_deadlocked_coroutines()), "\n";
$scope->cancel();
delay(10);
echo "end\n";
?>
--EXPECT--
found: 0
member: Scope was cancelled
waiter: Scope was cancelled
end
