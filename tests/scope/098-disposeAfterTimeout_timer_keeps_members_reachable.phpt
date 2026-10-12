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

$parked = 0;
$ended = 0;
$scope = new Scope();
$scope->spawn(function () {
    try {
        $GLOBALS['parked']++;
        await(new Future(new FutureState()));
    } catch (Async\AsyncCancellation $e) {
        echo "member: ", $e->getMessage(), "\n";
    }

    $GLOBALS['ended']++;
});
spawn(function () use ($scope) {
    try {
        $GLOBALS['parked']++;
        $scope->awaitCompletion(new Future(new FutureState()));
        echo "waiter: completed\n";
    } catch (Async\AsyncCancellation $e) {
        echo "waiter: ", $e->getMessage(), "\n";
    }

    $GLOBALS['ended']++;
});

// A coroutine runs on to its park, so the count means both have parked. A delay instead could end
// first: a timer due when main parks wakes it in place, ahead of the queue.
while ($parked < 2) {
    Async\suspend();
}

// Armed just before the check, which must come before the deadline
$scope->disposeAfterTimeout(100);
unset($scope);
echo "found: ", count(get_deadlocked_coroutines()), "\n";

while ($ended < 2) {
    delay(1);
}

echo "end\n";
?>
--EXPECT--
found: 0
member: Scope has been disposed due to timeout
waiter: Scope has been disposed due to timeout
end
