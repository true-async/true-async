--TEST--
Pool: release(null) of a pool that hands out objects is refused while one object is out
--FILE--
<?php

$pool = new Async\Pool(factory: fn() => new stdClass());
$held = $pool->tryAcquire();

try {
    $pool->release(null);
} catch (Async\PoolException $e) {
    echo $e->getMessage(), "\n";
}

echo "idle: ", $pool->idleCount(), ", active: ", $pool->activeCount(), "\n";

$pool->release($held);
echo "held one released\n";
?>
--EXPECT--
The resource was not acquired from this pool, or was released already
idle: 0, active: 1
held one released
