--TEST--
get_deadlocked_coroutines(): await_all() over dead items is found; over dead items and one live item it is not
--FILE--
<?php
use Async\Future;
use Async\FutureState;
use function Async\spawn;
use function Async\await_all;
use function Async\suspend;
use function Async\get_deadlocked_coroutines;

function start(Future $live): void
{
    $dead = [new Future(new FutureState()), new Future(new FutureState())];
    spawn(function () use ($dead) {
        await_all($dead);
    });

    $mixed = [new Future(new FutureState()), $live];
    spawn(function () use ($mixed) {
        [$results, $errors] = await_all($mixed);
        echo "mixed: ", count($results), " results\n";
    });
}

$live_state = new FutureState();
start(new Future($live_state));

for ($i = 0; $i < 4; $i++) {
    suspend();
}

foreach (get_deadlocked_coroutines() as $coroutine) {
    echo "found, parked at line ", $coroutine->getSuspendFileAndLine()[1], "\n";
    $coroutine->cancel();
}

foreach (Async\get_coroutines() as $coroutine) {
    if ($coroutine !== Async\current_coroutine()) {
        $coroutine->cancel();
    }
}

suspend();
echo "end\n";
?>
--EXPECT--
found, parked at line 13
end
