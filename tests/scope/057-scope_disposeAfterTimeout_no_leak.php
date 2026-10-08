<?php

use Async\Scope;
use function Async\spawn;
use function Async\delay;

// Timer fires: a child is parked in delay() when disposeAfterTimeout() trips.
spawn(function () {
    $scope = Scope::inherit()->asNotSafely();
    $scope->spawn(function () {
        try { delay(5000); }
        catch (\Throwable $e) { /* cancelled by the timeout */ }
    });
    $scope->disposeAfterTimeout(20);
    for ($i = 0; $i < 12; $i++) {
        delay(20);
    }
    echo "fired: finished=", ($scope->isFinished() ? "yes" : "no"), "\n";
});

echo "done\n";
?>
