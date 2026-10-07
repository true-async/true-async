--TEST--
get_deadlocked_coroutines(): a coroutine used as a WeakMap key is weakly referenced, so it and its partner are not reported
--FILE--
<?php
use function Async\spawn;
use function Async\await;
use function Async\suspend;
use function Async\get_deadlocked_coroutines;

$map = new WeakMap();

function start_pair(WeakMap $map): void
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
    $map[$b] = true;
}

start_pair($map);

for ($i = 0; $i < 4; $i++) {
    suspend();
}

echo count(get_deadlocked_coroutines()), " found\n";

foreach ($map as $coroutine => $value) {
    $coroutine->cancel();
}

suspend();
echo "end\n";
?>
--EXPECT--
0 found
end
