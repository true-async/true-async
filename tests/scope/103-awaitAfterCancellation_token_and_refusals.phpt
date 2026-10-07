--TEST--
Scope: awaitAfterCancellation() ends with OperationCanceledException when its token completes first, returns at once for a scope a cancel closed while idle, and throws in a scope's exception handler, which cannot park
--FILE--
<?php
use Async\Scope;
use function Async\spawn;
use function Async\await;
use function Async\delay;
use function Async\timeout;

$scope = Scope::inherit();
$scope->spawn(function () { delay(200); echo "zombie done\n"; });
Async\suspend();
$scope->cancel();

await(spawn(function () use ($scope) {
    try {
        $scope->awaitAfterCancellation(null, timeout(10));
    } catch (Async\OperationCanceledException $e) {
        echo get_class($e), "\n";
    }
}));

$empty = Scope::inherit();
$empty->spawn(function () {});
Async\suspend();
$empty->cancel();
await(spawn(function () use ($empty) {
    $empty->awaitAfterCancellation();
    echo "empty: returned\n";
}));

$failing = new Scope();
$failing->setExceptionHandler(function (Scope $failing, Async\Coroutine $coroutine, Throwable $e) use ($scope) {
    try {
        $scope->awaitAfterCancellation();
        echo "handler: returned\n";
    } catch (Throwable $error) {
        echo "handler: ", get_class($error), ": ", $error->getMessage(), "\n";
    }
});
$failing->spawn(function () {
    throw new RuntimeException("boom");
});
Async\suspend();
delay(10);
echo "end\n";
?>
--EXPECT--
Async\OperationCanceledException
empty: returned
handler: Error: awaitAfterCancellation() requires a running coroutine
end
zombie done
