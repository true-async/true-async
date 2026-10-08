--TEST--
get_deadlocked_coroutines() in a forked child before its first suspension: the parent's disposeAfterTimeout() timer is gone there, so the scope's stuck member and its waiter are found
--EXTENSIONS--
pcntl
--FILE--
<?php
use Async\Scope;
use Async\Future;
use Async\FutureState;
use function Async\await;
use function Async\spawn;
use function Async\get_deadlocked_coroutines;

$scope = new Scope();
$member = $scope->spawn(function () {
    await(new Future(new FutureState()));
});
$scope->disposeAfterTimeout(10000);
$waiter = spawn(function () use ($scope) {
    $scope->awaitCompletion(new Future(new FutureState()));
});
unset($scope);
while (!$member->isStarted() || !$waiter->isStarted()) {
    Async\suspend();
}
unset($member, $waiter);
echo "parent: ", count(get_deadlocked_coroutines()), "\n";

$pid = pcntl_fork();

if ($pid === 0) {
    echo "child: ", count(get_deadlocked_coroutines()), "\n";
    exit(0);
}

pcntl_waitpid($pid, $status);
echo "parent: child exit status ", pcntl_wexitstatus($status), "\n";
exit(0);
?>
--EXPECT--
parent: 0
child: 2
parent: child exit status 0
