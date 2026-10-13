--TEST--
Scope: awaitAfterCancellation() throws in a scope's exception handler, which cannot park
--FILE--
<?php
use Async\Scope;
use function Async\await;

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

$failing = new Scope();
$failing->setExceptionHandler(function (Scope $failing, Async\Coroutine $coroutine, Throwable $e) use ($scope) {
    try {
        $scope->awaitAfterCancellation();
        echo "handler: returned\n";
    } catch (Throwable $error) {
        echo "handler: ", get_class($error), ": ", $error->getMessage(), "\n";
    }
});
$thrower = $failing->spawn(function () {
    throw new RuntimeException("boom");
});

// A delay could end before the queued coroutine has run: a timer due when main parks wakes it in place
while (!$thrower->isCompleted()) {
    Async\suspend();
}

echo "end\n";
$release->complete(null);
?>
--EXPECT--
handler: Error: awaitAfterCancellation() requires a running coroutine
end
zombie done
