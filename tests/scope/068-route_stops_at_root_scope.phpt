--TEST--
Scope: an unhandled error in new Scope() cancels only that scope; unheld, it still ends the request
--FILE--
<?php

use Async\Scope;
use function Async\spawn;
use function Async\delay;

// The route does not leave the root: $outside is cancelled only by the request's end, which the unheld
// error starts.
$outside = spawn(function () {
    try {
        delay(20);
        echo "outside woke\n";
    } catch (Throwable $e) {
        echo "outside got ", get_class($e), ": ", $e->getMessage(), "\n";
    }
});

$scope = new Scope();
$member = $scope->spawn(function () {
    try {
        delay(50);
        echo "member woke\n";
    } catch (Throwable $e) {
        echo "member got ", get_class($e), ": ", $e->getMessage(), "\n";
    }
});
$scope->spawn(function () {
    throw new RuntimeException("unheld in root");
});

delay(30);
echo "not reached\n";

?>
--EXPECTF--
member got Async\AsyncCancellation: Coroutine cancelled
outside got Async\AsyncCancellation: Graceful shutdown

Fatal error: Uncaught RuntimeException: unheld in root in %s:%d
Stack trace:
#0 [internal function]: {closure:%s:%d}()
#1 {main}
  thrown in %s on line %d
