--TEST--
get_deadlocked_coroutines(): a coroutine awaiting a Future whose FutureState a coroutine waiting on a timer keeps is not reported
--FILE--
<?php
use Async\Future;
use Async\FutureState;
use function Async\spawn;
use function Async\suspend;
use function Async\delay;
use function Async\get_deadlocked_coroutines;

function start(): void
{
    $state = new FutureState();
    $future = new Future($state);
    spawn(function () use ($future) {
        $value = $future->await();
        echo "awaited: ", $value, "\n";
    });
    spawn(function () use ($state) {
        delay(20);
        $state->complete("value");
    });
}

start();

for ($i = 0; $i < 4; $i++) {
    suspend();
}

echo count(get_deadlocked_coroutines()), " found\n";
delay(50);
/* Both timers may fire in one tick, and the other waiter's wake then takes two more passes */
suspend();
suspend();
echo "end\n";
?>
--EXPECT--
0 found
awaited: value
end
