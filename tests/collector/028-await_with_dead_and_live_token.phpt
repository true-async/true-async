--TEST--
get_deadlocked_coroutines(): await() on a dead Future with a dead cancellation is found; with a cancellation main can complete it is not
--FILE--
<?php
use Async\Future;
use Async\FutureState;
use function Async\spawn;
use function Async\await;
use function Async\suspend;
use function Async\get_deadlocked_coroutines;

function start(Future $live): void
{
    $dead = new Future(new FutureState());
    $dead_token = new Future(new FutureState());
    spawn(function () use ($dead, $dead_token) {
        await($dead, $dead_token);
    });

    $other = new Future(new FutureState());
    spawn(function () use ($other, $live) {
        try {
            await($other, $live);
        } catch (Async\OperationCanceledException $e) {
            echo "live token: ", $e->getMessage(), "\n";
        }
    });
}

$live_state = new FutureState();
start(new Future($live_state));

for ($i = 0; $i < 4; $i++) {
    suspend();
}

$found = get_deadlocked_coroutines();

foreach ($found as $coroutine) {
    echo "found, parked at line ", $coroutine->getSuspendFileAndLine()[1], "\n";
    $coroutine->cancel();
}

$live_state->complete(null);
suspend();
suspend();
echo "end\n";
?>
--EXPECT--
found, parked at line 14
live token: Operation has been cancelled
end
