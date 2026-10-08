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
