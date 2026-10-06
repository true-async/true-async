--TEST--
get_deadlocked_coroutines(): three coroutines awaiting one another in a cycle
--FILE--
<?php
use function Async\spawn;
use function Async\await;
use function Async\suspend;
use function Async\get_deadlocked_coroutines;

function start_cycle(): void
{
    $holder = new stdClass();
    $holder->a = spawn(function () use ($holder) {
        suspend();
        await($holder->b);
    });
    $holder->b = spawn(function () use ($holder) {
        suspend();
        await($holder->c);
    });
    $holder->c = spawn(function () use ($holder) {
        suspend();
        await($holder->a);
    });
}

start_cycle();

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
3 found
end
