--TEST--
Scope: disposeAfterTimeout() of a closed scope stops a hanging finally handler that a finally handler's own coroutine registered, which starts after the first run ended
--FILE--
<?php

use function Async\delay;
use function Async\suspend;

$scope = new Async\Scope();
$in_finally = false;
$stopped = false;
$scope->spawn(function () use (&$in_finally) {
    Async\current_coroutine()->finally(function () use (&$in_finally) {
        Async\current_coroutine()->finally(function () {
            try {
                delay(100000);
            } catch (Async\AsyncCancellation $cancellation) {
                echo "nested finally stopped: ", $cancellation->getMessage(), "\n";
                $GLOBALS['stopped'] = true;
            }
        });
        $in_finally = true;
        delay(10);
    });
});

while (!$in_finally) {
    suspend();
}

$scope->cancel();
$scope->dispose();
$scope->disposeAfterTimeout(50);

// A longer delay could end first: a timer due when main parks wakes it in place, ahead of the queue
while (!$stopped) {
    delay(1);
}

echo "end\n";
?>
--EXPECT--
nested finally stopped: Scope has been disposed due to timeout
end
