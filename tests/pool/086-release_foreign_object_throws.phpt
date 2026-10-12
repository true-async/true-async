--TEST--
Pool: release() of an object the pool did not hand out throws, while another resource is out
--FILE--
<?php

$pool = new Async\Pool(factory: fn() => new stdClass(), max: 2);
$resource = $pool->tryAcquire();

try {
    $pool->release(new stdClass());
    echo "released\n";
} catch (Async\PoolException $e) {
    echo $e->getMessage(), "\n";
}

$pool->release($resource);
printf("idle=%d active=%d\n", $pool->idleCount(), $pool->activeCount());
?>
--EXPECT--
The resource was not acquired from this pool, or was released already
idle=1 active=0
