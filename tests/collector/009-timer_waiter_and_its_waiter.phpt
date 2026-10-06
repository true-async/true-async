--TEST--
get_deadlocked_coroutines(): a coroutine waiting on a timer and the one awaiting it are not reported
--FILE--
<?php
use function Async\spawn;
use function Async\await;
use function Async\suspend;
use function Async\get_deadlocked_coroutines;
use function Async\delay;

function start(): void
{
    $sleeper = spawn(function () {
        delay(50);
        echo "timer fired\n";
    });
    spawn(function () use ($sleeper) {
        await($sleeper);
        echo "waiter woke\n";
    });
}

start();
suspend();
suspend();
echo count(get_deadlocked_coroutines()), " found\n";
?>
--EXPECT--
0 found
timer fired
waiter woke
