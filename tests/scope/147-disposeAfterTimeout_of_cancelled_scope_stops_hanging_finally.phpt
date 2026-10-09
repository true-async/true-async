--TEST--
Scope: disposeAfterTimeout() of a cancelled scope stops its member's hanging finally handler
--FILE--
<?php

use function Async\delay;
use function Async\suspend;

$scope = new Async\Scope();
$started = false;
$in_finally = false;
$scope->spawn(function () use (&$started, &$in_finally) {
    Async\current_coroutine()->finally(function () use (&$in_finally) {
        $in_finally = true;

        try {
            delay(100000);
        } catch (Async\AsyncCancellation $cancellation) {
            echo "finally stopped: ", $cancellation->getMessage(), "\n";
        }
    });
    $started = true;
    delay(100000);
});

while (!$started) {
    suspend();
}

$scope->cancel();

while (!$in_finally) {
    suspend();
}

$scope->disposeAfterTimeout(20);
delay(60);
echo "end\n";
?>
--EXPECT--
finally stopped: Scope has been disposed due to timeout
end
