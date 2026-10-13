--TEST--
Scope: awaitAfterCancellation() ends with OperationCanceledException when its token completes first
--FILE--
<?php
use Async\Scope;
use function Async\spawn;
use function Async\await;
use function Async\timeout;

// The zombie outlives main by waiting on a Future main completes last; a delay could end before main
$release = new Async\FutureState();
$zombie_started = false;
$scope = Scope::inherit();
$scope->spawn(function () use ($release) {
    $GLOBALS['zombie_started'] = true;
    await(new Async\Future($release));
    echo "zombie done\n";
});

while (!$zombie_started) {
    Async\suspend();
}

$scope->cancel();

await(spawn(function () use ($scope) {
    try {
        $scope->awaitAfterCancellation(null, timeout(10));
    } catch (Async\OperationCanceledException $e) {
        echo get_class($e), "\n";
    }
}));

$release->complete(null);
?>
--EXPECT--
Async\OperationCanceledException
zombie done
