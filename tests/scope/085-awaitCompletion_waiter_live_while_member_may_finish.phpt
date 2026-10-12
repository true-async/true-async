--TEST--
get_deadlocked_coroutines(): a waiter in awaitCompletion() is not found while a member of the scope can still finish
--FILE--
<?php
use Async\Scope;
use Async\Future;
use Async\FutureState;
use function Async\spawn;
use function Async\delay;
use function Async\get_deadlocked_coroutines;

$parked = 0;
$checked = false;
$ended = false;
$scope = new Scope();
$scope->spawn(function () {
    $GLOBALS['parked']++;

    // Parked on a timer until main has checked: one delay could end before the check
    while (!$GLOBALS['checked']) {
        delay(1);
    }

    echo "member: done\n";
});
spawn(function () use ($scope) {
    $GLOBALS['parked']++;
    $scope->awaitCompletion(new Future(new FutureState()));
    echo "waiter: completed\n";
    $GLOBALS['ended'] = true;
});
unset($scope);

// A coroutine runs on to its park, so the count means both have parked
while ($parked < 2) {
    Async\suspend();
}

echo "found: ", count(get_deadlocked_coroutines()), "\n";
$checked = true;

while (!$ended) {
    delay(1);
}

echo "end\n";
?>
--EXPECT--
found: 0
member: done
waiter: completed
end
