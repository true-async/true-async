--TEST--
get_deadlocked_coroutines(): a waiter in awaitCompletion() is found with the scope's stuck member when only the waiter holds the Scope object
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
$memberId = $scope->spawn(function () {
    try {
        $GLOBALS['parked']++;
        await(new Future(new FutureState()));
    } catch (Async\AsyncCancellation $e) {
        echo "member: ", $e->getMessage(), "\n";
    }
    $GLOBALS['ended']++;
})->getId();
$waiterId = spawn(function () use ($scope) {
    try {
        $GLOBALS['parked']++;
        $scope->awaitCompletion(new Future(new FutureState()));
        echo "waiter: completed\n";
    } catch (Async\AsyncCancellation $e) {
        echo "waiter: ", $e->getMessage(), "\n";
    }
    $GLOBALS['ended']++;
})->getId();
unset($scope);

// A coroutine runs on to its park, so the count means both have parked. A delay instead could end
// first: a timer already due when main parks wakes it in that tick, before the queued coroutines.
while ($parked < 2) {
    Async\suspend();
}
$found = get_deadlocked_coroutines();
foreach ($found as $coroutine) {
    echo "found: ", $coroutine->getId() === $memberId ? "member" : ($coroutine->getId() === $waiterId ? "waiter" : "other"), "\n";
}
foreach ($found as $coroutine) {
    $coroutine->cancel();
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
waiter: Coroutine cancelled
end
