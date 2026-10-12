--TEST--
Pool: a second release() of an object, while another resource is out, throws and leaves the other's release valid
--FILE--
<?php

$pool = new Async\Pool(factory: fn() => new stdClass(), max: 2);
$first = $pool->tryAcquire();
$second = $pool->tryAcquire();

$pool->release($first);

try {
    $pool->release($first);
    echo "released twice\n";
} catch (Async\PoolException $e) {
    echo $e->getMessage(), "\n";
}

$pool->release($second);
printf("idle=%d active=%d\n", $pool->idleCount(), $pool->activeCount());
?>
--EXPECT--
The resource was not acquired from this pool, or was released already
idle=2 active=0
