--TEST--
get_deadlocked_coroutines(): a coroutine awaiting a Future whose FutureState its spawner dropped is found
--FILE--
<?php
use Async\Future;
use Async\FutureState;
use function Async\spawn;
use function Async\suspend;
use function Async\get_deadlocked_coroutines;

function start(): void
{
    $future = new Future(new FutureState());
    spawn(function () use ($future) {
        $future->await();
    });
}

start();

for ($i = 0; $i < 4; $i++) {
    suspend();
}

$found = get_deadlocked_coroutines();

foreach ($found as $coroutine) {
    echo "found, parked at line ", $coroutine->getSuspendFileAndLine()[1], "\n";
    $coroutine->cancel();
}

suspend();
echo "end\n";
?>
--EXPECT--
found, parked at line 12
end
