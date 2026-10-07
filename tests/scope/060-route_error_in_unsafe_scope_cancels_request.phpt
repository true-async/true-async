--TEST--
Scope: an unawaited error in a child of the global scope that does not dispose safely cancels every coroutine of the request
--FILE--
<?php

use Async\Scope;
use function Async\spawn;
use function Async\delay;

// p6.php of dev/plans/S9-scope.md 4: the route keeps the origin's flag up to the global scope, so main
// and $first are cancelled for real and $queued never runs. Nobody awaits $held: its error is reported
// at the end, as every unobserved one (TrueAsync drops it).
$first = spawn(function () {
    try {
        delay(50);
        echo "first woke\n";
    } catch (Throwable $e) {
        echo "first got ", get_class($e), ": ", $e->getMessage(), "\n";
    }
});

$scope = Scope::inherit()->asNotSafely();
$held = $scope->spawn(function () {
    throw new RuntimeException("x");
});
$queued = spawn(function () {
    echo "queued ran\n";
});

try {
    delay(100);
    echo "main woke\n";
} catch (Throwable $e) {
    echo "main got ", get_class($e), ": ", $e->getMessage(), "\n";
}

echo "queued cancelled: ", var_export($queued->isCancelled(), true), "\n";
echo "scope cancelled: ", var_export($scope->isCancelled(), true), "\n";
echo "end\n";

?>
--EXPECTF--
main got Async\AsyncCancellation: Coroutine cancelled
queued cancelled: true
scope cancelled: true
end
first got Async\AsyncCancellation: Coroutine cancelled

Fatal error: Uncaught RuntimeException: x in %s:%d
Stack trace:
#0 [internal function]: {closure:%s:%d}()
#1 {main}
  thrown in %s on line %d
