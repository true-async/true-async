--TEST--
get_deadlocked_coroutines(): a parked coroutine held only by the mapper of a map() child that nobody can complete is found with the child's waiter
--FILE--
<?php
use Async\Future;
use Async\FutureState;
use function Async\spawn;
use function Async\suspend;
use function Async\get_deadlocked_coroutines;

function start(): void
{
    $dead = new Future(new FutureState());
    $held = spawn(function () use ($dead) {
        $dead->await();
    });
    $source_state = new FutureState();
    $child = (new Future($source_state))->map(function ($value) use ($held) {
        return $value;
    });
    spawn(function () use ($child, $source_state) {
        $child->await();
    });
}

start();

for ($i = 0; $i < 4; $i++) {
    suspend();
}

foreach (get_deadlocked_coroutines() as $coroutine) {
    echo "found, parked at line ", $coroutine->getSuspendFileAndLine()[1], "\n";
    $coroutine->cancel();
}

suspend();
echo "end\n";
?>
--EXPECT--
found, parked at line 12
found, parked at line 19
end
