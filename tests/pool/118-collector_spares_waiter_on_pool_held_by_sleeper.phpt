--TEST--
Pool: the collector leaves alone a waiter on a pool a sleeping coroutine holds, and one whose resource a sleeper holds with the pool
--INI--
true_async.partial_deadlock=cancel
true_async.partial_deadlock_interval=0
--FILE--
<?php

use Async\Pool;
use function Async\delay;
use function Async\spawn;
use function Async\suspend;

function sleep_until_cancelled(): void
{
    try {
        delay(100000);
    } catch (Async\AsyncCancellation) {
    }
}

// The holder keeps the pool and wakes the waiter once its sleep ends.
$pool = new Pool(factory: fn() => "first", max: 1);
$held = $pool->acquire();
$parked = 0;

$waiter = spawn(function () use ($pool, &$parked) {
    $parked++;
    $resource = $pool->acquire();
    echo "held pool: $resource\n";
});
$holder = spawn(function () use ($pool, $held) {
    sleep_until_cancelled();
    $pool->release($held);
});

// The second pool reaches the sleeper only through the resource and the pool it holds.
$sleeper = spawn(function () use (&$parked) {
    $pool = new Pool(factory: fn() => "second", max: 1);
    $resource = $pool->acquire();
    spawn(function () use ($pool, &$parked) {
        $parked++;
        $resource = $pool->acquire();
        echo "pool of a sleeper: $resource\n";
    });
    sleep_until_cancelled();
    $pool->release($resource);
});
$pool = null;
$held = null;

while ($parked < 2) {
    suspend();
}

// Two automatic runs of the collector, at idle points: the second starts after both waiters parked.
$runs = TrueAsync\Test\collector_idle_runs();

while (TrueAsync\Test\collector_idle_runs() < $runs + 2) {
    delay(1);
}

$holder->cancel();
$sleeper->cancel();
suspend();
suspend();
echo "end\n";
?>
--EXPECT--
held pool: first
pool of a sleeper: second
end
