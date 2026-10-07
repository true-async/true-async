--TEST--
The automatic run: true_async.partial_deadlock=off runs no walk at the idle point, and get_deadlocked_coroutines() still works
--INI--
true_async.partial_deadlock=off
true_async.partial_deadlock_interval=0
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
delay(10);
delay(10);
var_dump(ini_set('true_async.partial_deadlock', 'kill'));

$found = get_deadlocked_coroutines();
echo count($found), " found\n";

foreach ($found as $coroutine) {
    $coroutine->cancel();
}
?>
--EXPECT--
bool(false)
2 found
