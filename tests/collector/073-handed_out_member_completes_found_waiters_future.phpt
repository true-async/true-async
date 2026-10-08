--TEST--
The fuzz oracle: a found member that an error route cancels completes a Future that a found coroutine outside the route's subtree waits on; the member runs only because the route woke it, so the wake is excused
--INI--
true_async.partial_deadlock_interval=0
--FILE--
<?php
use Async\Scope;
use Async\Future;
use Async\FutureState;
use function Async\await;
use function Async\suspend;
use function Async\delay;

$parent = (new Scope())->allowZombies();
$parent->setChildScopeExceptionHandler(function ($scope, $coroutine, $e) {
    echo "parent: took ", $e->getMessage(), "\n";
});
$scope = Scope::inherit($parent);
$state = new FutureState();
$future = new Future($state);
$member = $scope->spawn(function () use ($state) {
    try {
        await(new Future(new FutureState()));
    } finally {
        echo "member: completes the state\n";
        $state->complete(1);
    }
});
$other = (new Scope())->allowZombies();
$got = false;
$waiter = $other->spawn(function () use ($future, &$got) {
    $result = await($future);
    echo "waiter: got ", $result, "\n";
    $got = true;
});
$failing = Scope::inherit($scope)->asNotSafely();
$failer = $failing->spawn(function () {
    delay(50);
    throw new RuntimeException("failed");
});
while (!$member->isStarted() || !$waiter->isStarted() || !$failer->isStarted()) {
    suspend();
}
unset($parent, $scope, $other, $state, $future, $member, $waiter, $failer);
while (!$got) {
    delay(10);
}
echo "end\n";
?>
--EXPECTF--

Warning: Partial deadlock: coroutine #%d spawned at %s:16 can never wake (await: future) in %s on line 18

Warning: Partial deadlock: coroutine #%d spawned at %s:26 can never wake (await: future) in %s on line 27
parent: took failed
member: completes the state
waiter: got 1
end
