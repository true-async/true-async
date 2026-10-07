--TEST--
A zombie the graceful shutdown cancels for real leaves the core's coroutine count once it finishes
--FILE--
<?php

use Async\Scope;
use function Async\delay;
use function Async\graceful_shutdown;
use function Async\suspend;
use function TrueAsync\Test\coroutine_count;

$scope = Scope::inherit();
$zombie = $scope->spawn(function () {
    try {
        delay(100000);
    } catch (Async\AsyncCancellation $e) {
        echo "zombie: ", $e->getMessage(), "\n";
    }
});

suspend();
$scope->cancel();
echo "zombie: ", coroutine_count(), " ", var_export($zombie->isCompleted(), true), "\n";

graceful_shutdown();
suspend();

echo "after the shutdown: ", coroutine_count(), " ", var_export($zombie->isCompleted(), true), "\n";

?>
--EXPECT--
zombie: 1 false
zombie: Graceful shutdown
after the shutdown: 1 true
