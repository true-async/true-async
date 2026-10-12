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
        $GLOBALS['second'] = $future->await();
    });
}

$second = null;
start();

for ($i = 0; $i < 4; $i++) {
    suspend();
}

echo count(get_deadlocked_coroutines()), " found\n";

// Main prints what the second waiter got: the timeout can fire before the check on a loaded machine
while ($second === null) {
    delay(1);
}

echo "second waiter got: ", $second, "\n";
echo "end\n";
?>
--EXPECT--
0 found
second waiter got: late
end
