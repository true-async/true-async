--TEST--
get_deadlocked_coroutines(): a coroutine awaiting a FutureState kept only by the result of a Future it holds is found
--FILE--
<?php
use Async\Future;
use Async\FutureState;
use function Async\spawn;
use function Async\suspend;
use function Async\get_deadlocked_coroutines;

function start(): void
{
    $inner_state = new FutureState();
    $inner = new Future($inner_state);
    $outer_state = new FutureState();
    $outer_state->complete($inner_state);
    $outer_state->ignore();
    spawn(function () use ($outer_state, $inner) {
        $inner->await();
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
found, parked at line 16
end
