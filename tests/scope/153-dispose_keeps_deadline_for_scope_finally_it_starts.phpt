--TEST--
Scope: dispose() of a scope whose member ended keeps its disposeAfterTimeout() timer for the Scope::finally() handler the dispose starts, which the timer then stops
--FILE--
<?php

use function Async\delay;

$scope = new Async\Scope();
$scope->finally(function () {
    try {
        delay(100000);
    } catch (Async\AsyncCancellation $cancellation) {
        echo "finally stopped: ", $cancellation->getMessage(), "\n";
    }
});
$go = false;
$member = $scope->spawn(function () use (&$go) {
    while (!$go) {
        Async\suspend();
    }
});
$scope->disposeAfterTimeout(200);
$go = true;
Async\await($member);
$scope->dispose();
echo "closed: ", var_export($scope->isClosed(), true), "\n";
delay(300);
echo "end\n";
?>
--EXPECT--
closed: true
finally stopped: Scope has been disposed due to timeout
end
