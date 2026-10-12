--TEST--
get_deadlocked_coroutines(): a waiter in awaitCompletion() is found with a stuck member of a child scope, and the member's end still wakes it
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
$child = Scope::inherit($scope);
$memberId = $child->spawn(function () {
    try {
        $GLOBALS['parked']++;
        await(new Future(new FutureState()));
    } catch (Async\AsyncCancellation $e) {
        echo "member: ", $e->getMessage(), "\n";
    }

    $GLOBALS['ended']++;
})->getId();
$waiterId = spawn(function () use ($scope, $child) {
    $GLOBALS['parked']++;
    $scope->awaitCompletion(new Future(new FutureState()));
    echo "waiter: completed\n";
    $GLOBALS['ended']++;
})->getId();
unset($scope, $child);

// A coroutine runs on to its park, so the count means they have parked. A delay instead could end
// first: a timer due when main parks wakes it in place, ahead of the queue.
while ($parked < 2) {
    Async\suspend();
}

$found = get_deadlocked_coroutines();
foreach ($found as $coroutine) {
    echo "found: ", $coroutine->getId() === $memberId ? "member" : ($coroutine->getId() === $waiterId ? "waiter" : "other"), "\n";
}
// Only the member: its end completes the scope, which wakes the waiter the run found.
foreach ($found as $coroutine) {
    if ($coroutine->getId() === $memberId) {
        $coroutine->cancel();
    }
}
unset($found, $coroutine);

while ($ended < 2) {
    Async\suspend();
}

echo "end\n";
?>
--EXPECT--
found: member
found: waiter
member: Coroutine cancelled
waiter: completed
end
