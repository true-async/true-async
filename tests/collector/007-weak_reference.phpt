--TEST--
get_deadlocked_coroutines(): a coroutine reachable only through a WeakReference is not reported
--FILE--
<?php
use function Async\spawn;
use function Async\await;
use function Async\suspend;
use function Async\get_deadlocked_coroutines;

function start_pair(): WeakReference
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

    return WeakReference::create($a);
}

$weak = start_pair();

for ($i = 0; $i < 4; $i++) {
    suspend();
}

echo count(get_deadlocked_coroutines()), " found\n";
$weak->get()->cancel();
suspend();
echo "end\n";
?>
--EXPECT--
0 found
end
