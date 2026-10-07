--TEST--
get_deadlocked_coroutines(): a coroutine awaiting a map() child of a source main can complete is not reported; of a source only its waiter keeps it is found through the source's chain
--FILE--
<?php
use Async\Future;
use Async\FutureState;
use function Async\spawn;
use function Async\suspend;
use function Async\get_deadlocked_coroutines;

function start(Future $live): void
{
    $child_of_live = $live->map(fn ($value) => "mapped $value");
    spawn(function () use ($child_of_live) {
        echo $child_of_live->await(), "\n";
    });

    $dead_state = new FutureState();
    $child_of_dead = (new Future($dead_state))->map(fn ($value) => $value);
    spawn(function () use ($child_of_dead, $dead_state) {
        $child_of_dead->await();
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

$live_state->complete("value");

for ($i = 0; $i < 4; $i++) {
    suspend();
}

echo "end\n";
?>
--EXPECT--
found, parked at line 18
mapped value
end
