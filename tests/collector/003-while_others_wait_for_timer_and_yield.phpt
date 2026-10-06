--TEST--
get_deadlocked_coroutines(): a stuck pair is found while main waits on a timer and another coroutine yields
--FILE--
<?php
use function Async\spawn;
use function Async\await;
use function Async\suspend;
use function Async\get_deadlocked_coroutines;
use function Async\delay;

function start_pair(): void
{
    $a = null;
    $b = null;
    $a = spawn(function () use (&$b) {
        suspend();
        await($b);
    });
    $b = spawn(function () use (&$a) {
        suspend();
        await($a);
    });
}

start_pair();

$checker = spawn(function () {
    for ($i = 0; $i < 4; $i++) {
        suspend();
    }

    $found = get_deadlocked_coroutines();
    echo count($found), " found\n";

    foreach ($found as $coroutine) {
        $coroutine->cancel();
    }
});

delay(50);
await($checker);
echo "end\n";
?>
--EXPECT--
2 found
end
