--TEST--
The automatic run: a walk whose tables would take the memory in use past memory_limit stops and finds nothing, without a fatal error, and a later run under a higher limit warns
--INI--
true_async.partial_deadlock_interval=0
memory_limit=64M
--FILE--
<?php
use function Async\spawn;
use function Async\await;
use function Async\suspend;
use function Async\delay;
use function Async\get_deadlocked_coroutines;

function start_pair(): void
{
    $graph = [];

    for ($i = 0; $i < 300000; $i++) {
        $graph[] = new stdClass();
    }

    $a = null;
    $b = null;
    $a = spawn(function () use (&$b, $graph) {
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
echo "under 64M: nothing\n";
ini_set('memory_limit', '512M');
delay(10);
echo "under 512M: two warnings\n";

foreach (get_deadlocked_coroutines() as $coroutine) {
    $coroutine->cancel();
}
?>
--EXPECTF--
under 64M: nothing

Warning: Partial deadlock: coroutine #%d spawned at %s:%d can never wake (await: coroutine #%d) in %s on line %d

Warning: Partial deadlock: coroutine #%d spawned at %s:%d can never wake (await: coroutine #%d) in %s on line %d
under 512M: two warnings
