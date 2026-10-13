--TEST--
iterate(): a cancel of the caller's scope reaches the callbacks and the caller
--FILE--
<?php

use Async\Scope;
use function Async\await;
use function Async\delay;
use function Async\iterate;
use function Async\suspend;

$scope = new Scope();
$parked = 0;

$caller = $scope->spawn(function () use (&$parked) {
    try {
        iterate([1, 2], function ($value) use (&$parked) {
            $parked++;
            try {
                delay(10000);
            } catch (Async\AsyncCancellation $cancellation) {
                echo "callback $value: ", $cancellation->getMessage(), "\n";
                throw $cancellation;
            }
        });
        echo "iterate returned\n";
    } catch (Async\AsyncCancellation $cancellation) {
        echo "caller: ", $cancellation->getMessage(), "\n";
    }
});

while ($parked < 2) {
    suspend();
}

$scope->cancel();
await($caller);
echo "end\n";
?>
--EXPECT--
callback 1: Scope was cancelled
callback 2: Scope was cancelled
caller: Scope was cancelled
end
