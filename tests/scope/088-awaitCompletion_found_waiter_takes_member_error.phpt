--TEST--
get_deadlocked_coroutines(): a found waiter in awaitCompletion() takes the error of a found member that was cancelled and threw
--FILE--
<?php
use Async\Scope;
use Async\Future;
use Async\FutureState;
use function Async\await;
use function Async\spawn;
use function Async\get_deadlocked_coroutines;

$parked = 0;
$ended = false;
$scope = new Scope();
$memberId = $scope->spawn(function () {
    try {
        $GLOBALS['parked']++;
        await(new Future(new FutureState()));
    } catch (Async\AsyncCancellation $e) {
        throw new RuntimeException("member failed");
    }
})->getId();
spawn(function () use ($scope) {
    try {
        $GLOBALS['parked']++;
        $scope->awaitCompletion(new Future(new FutureState()));
        echo "waiter: completed\n";
    } catch (Throwable $e) {
        echo "waiter: ", get_class($e), ": ", $e->getMessage(), "\n";
    }

    $GLOBALS['ended'] = true;
});
unset($scope);

// A coroutine runs on to its park, so the count means they have parked. A delay instead could end
// first: a timer due when main parks wakes it in place, ahead of the queue.
while ($parked < 2) {
    Async\suspend();
}

$found = get_deadlocked_coroutines();
echo "found: ", count($found), "\n";
foreach ($found as $coroutine) {
    if ($coroutine->getId() === $memberId) {
        $coroutine->cancel();
    }
}
unset($found, $coroutine);

// The waiter ends after the member, whose error it takes
while (!$ended) {
    Async\suspend();
}

echo "end\n";
?>
--EXPECT--
found: 2
waiter: RuntimeException: member failed
end
