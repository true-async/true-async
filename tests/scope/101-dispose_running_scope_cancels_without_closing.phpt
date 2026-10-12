--TEST--
Scope: dispose() of a scope whose member runs cancels it without closing it, so a spawn is still accepted, as TrueAsync's ZEND_ASYNC_SCOPE_CLOSE (probe s9.5/d6); it closes once nothing runs
--FILE--
<?php
use Async\Scope;
use function Async\delay;

$parked = 0;
$ended = 0;
$scope = Scope::inherit()->asNotSafely();
$scope->spawn(function () {
    try {
        $GLOBALS['parked']++;
        delay(1000);
    } catch (Async\AsyncCancellation $e) {
        echo "member: ", $e->getMessage(), "\n";
    }

    $GLOBALS['ended']++;
});

// A coroutine runs on to its park, so the count means they have parked. A delay instead could end
// first: a timer due when main parks wakes it in place, ahead of the queue.
while ($parked < 1) {
    Async\suspend();
}

$scope->dispose();
printf("closed %d, cancelled %d, finished %d\n", $scope->isClosed(), $scope->isCancelled(), $scope->isFinished());
$scope->spawn(function () {
    echo "spawned after dispose() ran\n";
    $GLOBALS['ended']++;
});

while ($ended < 2) {
    Async\suspend();
}

printf("closed %d, cancelled %d, finished %d\n", $scope->isClosed(), $scope->isCancelled(), $scope->isFinished());
?>
--EXPECT--
closed 0, cancelled 1, finished 1
member: Scope was cancelled
spawned after dispose() ran
closed 1, cancelled 1, finished 1
