--TEST--
Scope: disposeAfterTimeout() of a closed scope stops a hanging finally handler that a finally handler's own coroutine registered, which starts after the first run ended
--FILE--
<?php

use function Async\delay;
use function Async\suspend;

$scope = new Async\Scope();
$in_finally = false;
$scope->spawn(function () use (&$in_finally) {
    Async\current_coroutine()->finally(function () use (&$in_finally) {
        Async\current_coroutine()->finally(function () {
            try {
                delay(100000);
            } catch (Async\AsyncCancellation $cancellation) {
                echo "nested finally stopped: ", $cancellation->getMessage(), "\n";
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
delay(100);
echo "end\n";
?>
--EXPECT--
nested finally stopped: Scope has been disposed due to timeout
end
