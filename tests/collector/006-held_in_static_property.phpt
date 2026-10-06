--TEST--
get_deadlocked_coroutines(): a parked coroutine holding one of a pair in a local array, the same coroutine also in a static property, is not reported, nor is the pair
--FILE--
<?php
use function Async\spawn;
use function Async\await;
use function Async\suspend;
use function Async\current_coroutine;
use function Async\get_deadlocked_coroutines;

final class Registry
{
    public static ?Async\Coroutine $kept = null;
}

function start(): void
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
    Registry::$kept = $b;
    spawn(function () use ($a, $b) {
        $pair = [$b];
        unset($b);
        await($a);
    });
}

start();

for ($i = 0; $i < 4; $i++) {
    suspend();
}

echo count(get_deadlocked_coroutines()), " found\n";

foreach (Async\get_coroutines() as $coroutine) {
    if ($coroutine !== current_coroutine()) {
        $coroutine->cancel();
    }
}

suspend();
echo "end\n";
?>
--EXPECT--
0 found
end
