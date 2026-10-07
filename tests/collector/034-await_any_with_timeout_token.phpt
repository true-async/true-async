--TEST--
get_deadlocked_coroutines(): a coroutine in await_any_or_fail() over a dead Future with a Timeout as its token is not reported, and the Timeout ends its wait
--FILE--
<?php
use Async\Future;
use Async\FutureState;
use function Async\spawn;
use function Async\suspend;
use function Async\delay;
use function Async\timeout;
use function Async\await_any_or_fail;
use function Async\get_deadlocked_coroutines;

function start(): void
{
    $future = new Future(new FutureState());
    spawn(function () use ($future) {
        try {
            await_any_or_fail([$future], timeout(30));
        } catch (Throwable $e) {
            echo "wait ended: ", $e::class, "\n";
        }
    });
}

start();

for ($i = 0; $i < 4; $i++) {
    suspend();
}

echo count(get_deadlocked_coroutines()), " found\n";
delay(60);
echo "end\n";
?>
--EXPECT--
0 found
wait ended: Async\OperationCanceledException
end
