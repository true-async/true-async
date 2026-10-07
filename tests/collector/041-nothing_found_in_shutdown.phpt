--TEST--
get_deadlocked_coroutines(): once the request shuts down nothing is found, since the engine calls every remaining destructor whatever holds its object
--FILE--
<?php
use Async\Future;
use Async\FutureState;
use function Async\spawn;
use function Async\suspend;
use function Async\current_coroutine;
use function Async\get_coroutines;
use function Async\get_deadlocked_coroutines;

function make(): void
{
    $state = new FutureState();
    $state->ignore();
    spawn(function () use ($state) {
        (new Future($state))->await();
    });
}

register_shutdown_function(function () {
    make();
    suspend();
    suspend();
    echo count(get_deadlocked_coroutines()), " found in a shutdown function\n";

    foreach (get_coroutines() as $coroutine) {
        if ($coroutine !== current_coroutine()) {
            $coroutine->cancel();
        }
    }
});
echo "end\n";
?>
--EXPECT--
end
0 found in a shutdown function
