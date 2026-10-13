--TEST--
Scope: awaitCompletion() in a scope's exception handler throws, as the handler cannot park
--DESCRIPTION--
A departure from TrueAsync, whose handler parks the finished coroutine (dev/plans/S9-scope.md 9, item 9).
--FILE--
<?php

use Async\Scope;
use function Async\delay;

$other = Scope::inherit();
// Runs until main ends: the handler must find the scope not completed.
$ended = false;
$other->spawn(function () {
    while (!$GLOBALS['ended']) {
        delay(1);
    }
});

$scope = Scope::inherit();
$scope->setExceptionHandler(function (Scope $scope, Async\Coroutine $coroutine, Throwable $e) use ($other) {
    try {
        $other->awaitCompletion(new Async\Future(new Async\FutureState()));
        echo "handler: returned\n";
    } catch (Throwable $error) {
        echo "handler: ", get_class($error), ": ", $error->getMessage(), "\n";
    }
});

$failing = $scope->spawn(function () {
    throw new RuntimeException("boom");
});

// A delay could end before the queued coroutine has run: a timer due when main parks wakes it in place
while (!$failing->isCompleted()) {
    Async\suspend();
}

echo "end\n";
$ended = true;

?>
--EXPECT--
handler: Error: awaitCompletion() requires a running coroutine
end
