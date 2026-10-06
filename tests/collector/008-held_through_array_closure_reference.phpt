--TEST--
get_deadlocked_coroutines(): a parked coroutine holding one of a pair through an array, a closure's use and a reference is reported with the pair, and not when a static property also holds it
--FILE--
<?php
use function Async\spawn;
use function Async\await;
use function Async\suspend;
use function Async\current_coroutine;
use function Async\get_deadlocked_coroutines;

final class Registry
{
    public static array $kept = [];
}

function start_group(bool $also_static): void
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

    if ($also_static) {
        Registry::$kept[] = $b;
    }

    spawn(function () use ($a, $b) {
        $in_array = ['pair' => [$b]];
        $closure = function () use ($b) {
            return $b;
        };
        $value = $b;
        $reference = &$value;
        unset($b);
        await($a);
    });
}

start_group(false);
start_group(true);

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
3 found
end
