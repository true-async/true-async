--TEST--
The automatic run: with E_WARNING out of the INI error_reporting nothing is raised, and a run after ini_set() turns it back on warns
--INI--
true_async.partial_deadlock_interval=0
error_reporting=E_ALL & ~E_WARNING
--FILE--
<?php
use function Async\spawn;
use function Async\await;
use function Async\suspend;
use function Async\delay;
use function Async\get_deadlocked_coroutines;

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
echo "E_WARNING off: nothing\n";
ini_set('error_reporting', (string) E_ALL);
delay(10);
echo "E_WARNING on: two warnings\n";

foreach (get_deadlocked_coroutines() as $coroutine) {
    $coroutine->cancel();
}
?>
--EXPECTF--
E_WARNING off: nothing

Warning: Partial deadlock: coroutine #%d spawned at %s:%d can never wake (await: coroutine #%d) in %s on line %d

Warning: Partial deadlock: coroutine #%d spawned at %s:%d can never wake (await: coroutine #%d) in %s on line %d
E_WARNING on: two warnings
