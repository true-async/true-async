--TEST--
Scope: exit() in a handler ends the request as exit() in a coroutine does
--DESCRIPTION--
A departure from TrueAsync, which chains the exit as a handler's exception and loses its status
(dev/plans/S9-scope.md 9).
--FILE--
<?php

use Async\Scope;
use function Async\spawn;
use function Async\delay;

register_shutdown_function(function () {
    echo "shutdown\n";
});

$scope = Scope::inherit()->asNotSafely();
$scope->setExceptionHandler(function (Scope $scope, Async\Coroutine $coroutine, Throwable $e) {
    echo "handler exits\n";
    exit(3);
});

$sibling = $scope->spawn(function () {
    try {
        delay(50);
        echo "sibling woke\n";
    } catch (Throwable $e) {
        echo "sibling got ", get_class($e), ": ", $e->getMessage(), "\n";
    }
});
$scope->spawn(function () {
    throw new RuntimeException("boom");
});

try {
    delay(100);
    echo "main woke\n";
} catch (Throwable $e) {
    echo "main got ", get_class($e), ": ", $e->getMessage(), "\n";
}

echo "end\n";

?>
--EXPECT--
handler exits
main got Async\AsyncCancellation: Graceful shutdown
end
sibling got Async\AsyncCancellation: Graceful shutdown
shutdown
