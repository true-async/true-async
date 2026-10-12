--TEST--
true_async.partial_deadlock=cancel: a waiter in awaitCompletion() and the stuck member are warned about and cancelled
--INI--
true_async.partial_deadlock=cancel
true_async.partial_deadlock_interval=0
--FILE--
<?php
use Async\Scope;
use Async\Future;
use Async\FutureState;
use function Async\await;
use function Async\spawn;
use function Async\delay;

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
unset($scope);

wait_for_idle_run();
echo "end\n";
// A delay can end before the scheduler goes idle: a timer due when main parks wakes it in place
function wait_for_idle_run(): void
{
    $runs = TrueAsync\Test\collector_idle_runs();

    while (TrueAsync\Test\collector_idle_runs() === $runs) {
        delay(1);
    }
}
?>
--EXPECTF--

Warning: Partial deadlock: coroutine #%d spawned at %s:10 can never wake (await: future) in %s on line 12

Warning: Partial deadlock: coroutine #%d spawned at %s:17 can never wake (await: scope created at %s:9) in %s on line 19
member: Deadlock detected
waiter: Deadlock detected
end
