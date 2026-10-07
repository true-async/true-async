--TEST--
get_deadlocked_coroutines(): two coroutines awaiting the two map() children of a source only they keep are both found through the source's chain
--FILE--
<?php
use Async\Future;
use Async\FutureState;
use function Async\spawn;
use function Async\suspend;
use function Async\get_deadlocked_coroutines;

function start(): void
{
    $state = new FutureState();
    $source = new Future($state);
    $first = $source->map(fn ($value) => $value);
    $second = $source->map(fn ($value) => $value);

    spawn(function () use ($first, $state) {
        $first->await();
    });
    spawn(function () use ($second, $state) {
        $second->await();
    });
}

start();

for ($i = 0; $i < 4; $i++) {
    suspend();
}

$found = get_deadlocked_coroutines();
echo count($found), " found\n";

foreach ($found as $coroutine) {
    $coroutine->cancel();
}
?>
--EXPECT--
2 found
