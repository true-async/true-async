--TEST--
get_deadlocked_coroutines(): a disposeAfterTimeout() timer armed on a cancelled scope keeps the finally handler waiting below it, which the timer's fire stops, from being found
--FILE--
<?php

use Async\Future;
use Async\FutureState;
use function Async\await;
use function Async\delay;
use function Async\get_deadlocked_coroutines;
use function Async\suspend;

$scope = new Async\Scope();
$in_finally = false;
$scope->spawn(function () use (&$in_finally) {
    Async\current_coroutine()->finally(function () use (&$in_finally) {
        $in_finally = true;

        try {
            await(new Future(new FutureState()));
        } catch (Async\AsyncCancellation $cancellation) {
            echo "finally stopped: ", $cancellation->getMessage(), "\n";
        }
    });
});

while (!$in_finally) {
    suspend();
}

$scope->cancel();
$scope->disposeAfterTimeout(100);
unset($scope);
echo "found: ", count(get_deadlocked_coroutines()), "\n";
delay(200);
echo "end\n";
?>
--EXPECT--
found: 0
finally stopped: Scope has been disposed due to timeout
end
