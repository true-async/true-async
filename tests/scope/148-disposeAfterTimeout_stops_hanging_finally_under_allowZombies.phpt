--TEST--
Scope: disposeAfterTimeout() of a scope that allows zombies stops its returned member's hanging finally handler, not leaving it a zombie
--FILE--
<?php

use function Async\delay;
use function Async\suspend;

$scope = (new Async\Scope())->allowZombies();
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

$scope->disposeAfterTimeout(20);
delay(60);
echo "end\n";
?>
--EXPECT--
finally stopped: Scope has been disposed due to timeout
end
