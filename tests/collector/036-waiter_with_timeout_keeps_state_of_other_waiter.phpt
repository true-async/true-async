--TEST--
get_deadlocked_coroutines(): of two coroutines awaiting one FutureState, the one with a Timeout keeps the other live, and completes the state after its timer
--FILE--
<?php
use Async\Future;
use Async\FutureState;
use function Async\spawn;
use function Async\suspend;
use function Async\delay;
use function Async\timeout;
use function Async\get_deadlocked_coroutines;

function start(): void
{
    $state = new FutureState();
    $future = new Future($state);
    spawn(function () use ($state, $future) {
        try {
            $future->await(timeout(30));
        } catch (Async\OperationCanceledException) {
            $state->complete("late");
        }
    });
    spawn(function () use ($future) {
        $value = $future->await();
        echo "second waiter got: ", $value, "\n";
    });
}

start();

for ($i = 0; $i < 4; $i++) {
    suspend();
}

echo count(get_deadlocked_coroutines()), " found\n";
delay(60);
/* Both timers may fire in one tick, and the other waiter's wake then takes two more passes */
suspend();
suspend();
echo "end\n";
?>
--EXPECT--
0 found
second waiter got: late
end
