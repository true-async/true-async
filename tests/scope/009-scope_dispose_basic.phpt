--TEST--
Scope: dispose() and disposeSafely() - basic usage
--FILE--
<?php

use Async\Scope;
use Async\Future;
use Async\FutureState;
use function Async\await;
use function Async\suspend;

$started = 0;

$scope1 = new Scope();
$member1 = $scope1->spawn(function () use (&$started) {
    $started++;
    try {
        await(new Future(new FutureState()));
    } catch (Async\AsyncCancellation) {
        return "dispose cancelled the member";
    }
});

$scope2 = new Scope();
$member2 = $scope2->spawn(function () use (&$started) {
    $started++;
    suspend();
    suspend();
    return "disposeSafely let the member finish";
});

// A member the dispose finds unstarted would never run its body.
while ($started < 2) {
    suspend();
}

$scope1->dispose();
$scope2->disposeSafely();

echo await($member1), "\n";
echo await($member2), "\n";

echo "Dispose methods executed without errors\n";

?>
--EXPECT--
dispose cancelled the member
disposeSafely let the member finish
Dispose methods executed without errors