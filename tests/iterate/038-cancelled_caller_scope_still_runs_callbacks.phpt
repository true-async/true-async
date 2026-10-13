--TEST--
iterate(): a caller whose scope is cancelled still runs the callbacks
--FILE--
<?php

use Async\Scope;
use function Async\await;
use function Async\delay;
use function Async\iterate;
use function Async\suspend;

$scope = new Scope();
$parked = false;

$caller = $scope->spawn(function () use (&$parked) {
    try {
        $parked = true;
        delay(10000);
    } catch (Async\AsyncCancellation) {
        echo "caller cancelled\n";
    }
    iterate([1, 2], function ($value) {
        echo "callback $value\n";
    });
    echo "iterate returned\n";
});

while (!$parked) {
    suspend();
}

$scope->cancel();
await($caller);
echo "end\n";
?>
--EXPECT--
caller cancelled
callback 1
callback 2
iterate returned
end
