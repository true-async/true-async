--TEST--
get_deadlocked_coroutines(): members of a scope whose Scope object main holds are not found, since main can cancel them through the scope
--FILE--
<?php
use Async\Scope;
use Async\Future;
use Async\FutureState;
use function Async\await;
use function Async\suspend;
use function Async\delay;
use function Async\get_deadlocked_coroutines;

$scope = new Scope();
$a = null;
$b = null;
$a = $scope->spawn(function () use (&$b) {
    try {
        suspend();
        await($b);
    } catch (Async\AsyncCancellation $e) {
        echo "a: ", $e->getMessage(), "\n";
    }
});
$b = $scope->spawn(function () use (&$a) {
    try {
        suspend();
        await($a);
    } catch (Async\AsyncCancellation $e) {
        echo "b: ", $e->getMessage(), "\n";
    }
});
$scope->spawn(function () {
    try {
        await(new Future(new FutureState()));
    } catch (Async\AsyncCancellation $e) {
        echo "c: ", $e->getMessage(), "\n";
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
a: Scope was cancelled
b: Scope was cancelled
c: Scope was cancelled
end
