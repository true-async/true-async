--TEST--
get_deadlocked_coroutines(): a coroutine kept in a local variable of a coroutine that runs is not reported
--FILE--
<?php
use function Async\spawn;
use function Async\await;
use function Async\suspend;
use function Async\get_deadlocked_coroutines;

function start_pair(): Async\Coroutine
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

    return $b;
}

function check(): void
{
    $local = start_pair();

    for ($i = 0; $i < 4; $i++) {
        suspend();
    }

    echo count(get_deadlocked_coroutines()), " found while the local holds one\n";
    $local->cancel();
}

check();
suspend();
echo "end\n";
?>
--EXPECT--
0 found while the local holds one
end
