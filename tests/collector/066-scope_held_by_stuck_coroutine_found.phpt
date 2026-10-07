--TEST--
get_deadlocked_coroutines(): members of a scope whose Scope object only a stuck coroutine holds are found with it
--FILE--
<?php
use Async\Scope;
use Async\Future;
use Async\FutureState;
use function Async\spawn;
use function Async\await;
use function Async\delay;
use function Async\get_deadlocked_coroutines;

function start(): void
{
    $scope = new Scope();
    $scope->spawn(function () {
        await(new Future(new FutureState()));
    });
    spawn(function () use ($scope) {
        await(new Future(new FutureState()));
        $scope->cancel();
    });
}

start();
delay(10);
$found = get_deadlocked_coroutines();
echo "found: ", count($found), "\n";
foreach ($found as $coroutine) {
    $coroutine->cancel();
}
delay(10);
echo "end\n";
?>
--EXPECT--
found: 2
end
