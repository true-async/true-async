--TEST--
Pool: release() with nothing handed out throws and the counts stay as they were
--FILE--
<?php

use Async\Pool;
use Async\PoolException;

$pool = new Pool(factory: fn() => 1, min: 1, max: 2);

try {
    $pool->release(1);
    echo "released\n";
} catch (PoolException $e) {
    echo $e->getMessage(), "\n";
}

printf("idle=%d active=%d count=%d\n", $pool->idleCount(), $pool->activeCount(), count($pool));
?>
--EXPECT--
The resource was not acquired from this pool, or was released already
idle=1 active=0 count=1
