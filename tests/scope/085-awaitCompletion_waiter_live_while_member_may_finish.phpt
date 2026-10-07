--TEST--
get_deadlocked_coroutines(): a waiter in awaitCompletion() is not found while a member of the scope can still finish
--FILE--
<?php
use Async\Scope;
use Async\Future;
use Async\FutureState;
use function Async\spawn;
use function Async\delay;
use function Async\get_deadlocked_coroutines;

$scope = new Scope();
$scope->spawn(function () {
    delay(50);
    echo "member: done\n";
});
spawn(function () use ($scope) {
    $scope->awaitCompletion(new Future(new FutureState()));
    echo "waiter: completed\n";
});
unset($scope);

delay(10);
echo "found: ", count(get_deadlocked_coroutines()), "\n";
delay(100);
echo "end\n";
?>
--EXPECT--
found: 0
member: done
waiter: completed
end
