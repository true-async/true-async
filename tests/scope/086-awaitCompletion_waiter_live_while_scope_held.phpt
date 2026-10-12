--TEST--
get_deadlocked_coroutines(): neither the stuck member nor the waiter in awaitCompletion() is found while main holds the Scope object
--FILE--
<?php
use Async\Scope;
use Async\Future;
use Async\FutureState;
use function Async\await;
use function Async\spawn;
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

// A coroutine runs on to its park, so the count means they have parked. A delay instead could end
// first: a timer due when main parks wakes it in place, ahead of the queue.
while ($parked < 2) {
    Async\suspend();
}

echo "found: ", count(get_deadlocked_coroutines()), "\n";
$scope->cancel();

while ($ended < 2) {
    Async\suspend();
}

echo "end\n";
?>
--EXPECT--
found: 0
member: Scope was cancelled
waiter: Scope was cancelled
end
