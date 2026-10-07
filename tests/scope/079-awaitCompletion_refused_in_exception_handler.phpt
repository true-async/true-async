--TEST--
Scope: awaitCompletion() in a scope's exception handler throws, as the handler cannot park
--DESCRIPTION--
A departure from TrueAsync, whose handler parks the finished coroutine (dev/plans/S9-scope.md 9, item 9).
--FILE--
<?php

use Async\Scope;
use function Async\delay;
use function Async\timeout;

$other = Scope::inherit();
$other->spawn(fn() => delay(50));

$scope = Scope::inherit();
$scope->setExceptionHandler(function (Scope $scope, Async\Coroutine $coroutine, Throwable $e) use ($other) {
    try {
        $other->awaitCompletion(timeout(1000));
        echo "handler: returned\n";
    } catch (Throwable $error) {
        echo "handler: ", get_class($error), ": ", $error->getMessage(), "\n";
    }
});

$scope->spawn(function () {
    throw new RuntimeException("boom");
});

delay(100);
echo "end\n";

?>
--EXPECT--
handler: Error: awaitCompletion() requires a running coroutine
end
