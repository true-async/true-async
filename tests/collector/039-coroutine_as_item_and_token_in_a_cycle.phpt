--TEST--
get_deadlocked_coroutines(): a cycle through a coroutine awaited as an await_all() item, and one through a coroutine used as a token, are found
--FILE--
<?php
use Async\Future;
use Async\FutureState;
use function Async\spawn;
use function Async\await;
use function Async\await_all;
use function Async\suspend;
use function Async\get_deadlocked_coroutines;

function start(): void
{
    $items = new stdClass();
    $items->waiter = spawn(function () use ($items) {
        await_all([$items->item]);
    });
    $items->item = spawn(function () use ($items) {
        await($items->waiter);
    });

    $tokens = new stdClass();
    $tokens->future = new Future(new FutureState());
    $tokens->waiter = spawn(function () use ($tokens) {
        $tokens->future->await($tokens->token);
    });
    $tokens->token = spawn(function () use ($tokens) {
        await($tokens->waiter);
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
found, parked at line 14
found, parked at line 17
found, parked at line 23
found, parked at line 26
end
