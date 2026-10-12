--TEST--
Scope: disposeAfterTimeout() of a scope that allows zombies stops its returned member's hanging finally handler, not leaving it a zombie
--FILE--
<?php

use function Async\delay;
use function Async\suspend;

$scope = (new Async\Scope())->allowZombies();
$in_finally = false;
$stopped = false;
$scope->spawn(function () use (&$in_finally) {
    Async\current_coroutine()->finally(function () use (&$in_finally) {
        $in_finally = true;

        try {
            delay(100000);
        } catch (Async\AsyncCancellation $cancellation) {
            echo "finally stopped: ", $cancellation->getMessage(), "\n";
            $GLOBALS['stopped'] = true;
        }
    });
});

while (!$in_finally) {
    suspend();
}

$scope->disposeAfterTimeout(20);

// A longer delay could end first: a timer due when main parks wakes it in place, ahead of the queue
while (!$stopped) {
    delay(1);
}

echo "end\n";
?>
--EXPECT--
finally stopped: Scope has been disposed due to timeout
end
