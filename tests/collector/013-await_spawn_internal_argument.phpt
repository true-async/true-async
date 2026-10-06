--TEST--
get_deadlocked_coroutines(): a coroutine held only by await()'s argument, await(spawn(...)), is counted through the internal frame
--FILE--
<?php
use function Async\spawn;
use function Async\await;
use function Async\suspend;
use function Async\current_coroutine;
use function Async\get_deadlocked_coroutines;

function start(): void
{
    spawn(function () {
        $me = current_coroutine();
        await(spawn(function () use ($me) {
            await($me);
        }));
    });
}

start();

for ($i = 0; $i < 4; $i++) {
    suspend();
}

$found = get_deadlocked_coroutines();
echo count($found), " found\n";

foreach ($found as $coroutine) {
    $coroutine->cancel();
}

echo "end\n";
?>
--EXPECT--
2 found
end
