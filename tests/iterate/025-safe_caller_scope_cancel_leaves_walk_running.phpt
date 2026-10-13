--TEST--
iterate(): a safe cancel of the caller's scope leaves the walk running to its end
--FILE--
<?php

use Async\Scope;
use function Async\await;
use function Async\iterate;
use function Async\suspend;

$scope = (new Scope())->allowZombies();
$parked = 0;
$cancelled = false;

$caller = $scope->spawn(function () use (&$parked, &$cancelled) {
    iterate([1, 2], function ($value) use (&$parked, &$cancelled) {
        $parked++;
        while (!$cancelled) {
            suspend();
        }
        echo "callback $value finished\n";
    });
    echo "iterate returned\n";
});

while ($parked < 2) {
    suspend();
}

$scope->cancel();
$cancelled = true;
await($caller);
echo "caller cancelled: ", var_export($caller->isCancelled(), true), "\n";
?>
--EXPECT--
callback 1 finished
callback 2 finished
iterate returned
caller cancelled: true
