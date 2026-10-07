--TEST--
Scope: awaitCompletion() - must complete before timeout when coroutines finish quickly
--XFAIL--
Not implemented yet: S9.4 of dev/PLAN.md
--FILE--
<?php

use Async\Scope;
use function Async\spawn;
use function Async\await;
use function Async\timeout;

echo "start\n";

// Coroutines that finish immediately must not cause awaitCompletion to wait for the full timeout.
// If scope fails to notify completion, OperationCanceledException will be thrown.
$scope = new Scope();

$scope->spawn(function () {
    echo "coroutine running\n";
});

$external = spawn(function () use ($scope) {
    try {
        $scope->awaitCompletion(timeout(500));
        echo "completed without timeout\n";
    } catch (\Async\OperationCanceledException $e) {
        echo "ERROR: timed out instead of completing\n";
    }
});

await($external);

echo "end\n";

?>
--EXPECT--
start
coroutine running
completed without timeout
end
