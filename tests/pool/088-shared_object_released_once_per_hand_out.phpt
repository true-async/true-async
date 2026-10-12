--TEST--
Pool: one object the factory hands out twice is taken back twice, and a third release throws
--FILE--
<?php

$client = new stdClass();
$pool = new Async\Pool(factory: fn() => $client, max: 2);

$pool->tryAcquire();
$pool->tryAcquire();

$pool->release($client);
$pool->release($client);
echo "released twice\n";

try {
    $pool->release($client);
} catch (Async\PoolException $e) {
    echo $e->getMessage(), "\n";
}

printf("idle=%d active=%d\n", $pool->idleCount(), $pool->activeCount());
?>
--EXPECT--
released twice
The resource was not acquired from this pool, or was released already
idle=2 active=0
