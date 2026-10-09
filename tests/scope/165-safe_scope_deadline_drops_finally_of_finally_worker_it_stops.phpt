--TEST--
Scope: in a safe scope, the Coroutine::finally() handler that a finally handler stopped by the disposeAfterTimeout() fire added to its worker is not called
--FILE--
<?php

use function Async\delay;
use function Async\suspend;

$scope = Async\Scope::inherit();
$in_finally = false;
$scope->spawn(function () use (&$in_finally) {
    Async\current_coroutine()->finally(function () use (&$in_finally) {
        Async\current_coroutine()->finally(function () {
            echo "nested finally runs\n";
        });
        $in_finally = true;

        try {
            delay(100000);
        } catch (Async\AsyncCancellation $cancellation) {
            echo "finally stopped\n";
        }
    });
});

while (!$in_finally) {
    suspend();
}

$scope->disposeAfterTimeout(10);
delay(60);
echo "end\n";
?>
--EXPECT--
finally stopped
end
