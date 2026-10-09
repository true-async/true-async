--TEST--
Scope: disposeAfterTimeout() after cancel() and dispose() closed the scope stops its member's hanging finally handler
--FILE--
<?php

use function Async\delay;
use function Async\suspend;

$scope = new Async\Scope();
$in_finally = false;
$scope->spawn(function () use (&$in_finally) {
    Async\current_coroutine()->finally(function () use (&$in_finally) {
        $in_finally = true;

        try {
            delay(100000);
        } catch (Async\AsyncCancellation $cancellation) {
            echo "finally stopped: ", $cancellation->getMessage(), "\n";
        }
    });
});

while (!$in_finally) {
    suspend();
}

$scope->cancel();
$scope->dispose();
echo "closed: ", var_export($scope->isClosed(), true), "\n";
$scope->disposeAfterTimeout(20);
delay(60);
echo "end\n";
?>
--EXPECT--
closed: true
finally stopped: Scope has been disposed due to timeout
end
