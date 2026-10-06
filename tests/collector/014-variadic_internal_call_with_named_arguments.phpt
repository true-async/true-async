--TEST--
get_deadlocked_coroutines(): a coroutine parked under call_user_func() with named arguments on its stack
--FILE--
<?php
use function Async\spawn;
use function Async\await;
use function Async\suspend;
use function Async\get_deadlocked_coroutines;

function start_pair(): void
{
    $a = null;
    $b = null;
    $a = spawn(function () use (&$b) {
        suspend();
        call_user_func('Async\await', awaitable: $b);
    });
    $b = spawn(function () use (&$a) {
        suspend();
        await($a);
    });
}

start_pair();

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
