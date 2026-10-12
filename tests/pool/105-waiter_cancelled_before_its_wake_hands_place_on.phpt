--TEST--
Pool: a waiter cancelled and then promised a place before it runs hands the place to the next waiter
--FILE--
<?php

use Async\Pool;
use function Async\spawn;
use function Async\suspend;

$pool = new Pool(factory: fn() => new stdClass(), max: 1);
$held = $pool->acquire();
$parked = 0;

$first = spawn(function () use ($pool, &$parked) {
    $parked++;
    try {
        $pool->acquire();
        echo "first: acquired\n";
    } catch (Async\AsyncCancellation $cancellation) {
        echo "first: ", $cancellation->getMessage(), "\n";
    }
});
$second = spawn(function () use ($pool, &$parked) {
    $parked++;
    $resource = $pool->acquire();
    echo "second: acquired\n";
    $pool->release($resource);
});

while ($parked < 2) {
    suspend();
}

// The cancel queues the first waiter; the release, before it runs, promises it the place.
$first->cancel();
$pool->release($held);

Async\await($second);
echo "idle: ", $pool->idleCount(), ", count: ", count($pool), "\n";
?>
--EXPECT--
first: Coroutine cancelled
second: acquired
idle: 1, count: 1
