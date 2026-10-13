--TEST--
Pool: a second release() of a scalar the pool handed out once is refused
--FILE--
<?php

use Async\Pool;

$pool = new Pool(factory: fn() => 42);
$value = $pool->acquire();
$pool->release($value);

try {
    $pool->release($value);
} catch (Async\PoolException $exception) {
    echo $exception->getMessage(), "\n";
}

echo "active: ", $pool->activeCount(), ", idle: ", $pool->idleCount(), "\n";
?>
--EXPECT--
The resource was not acquired from this pool, or was released already
active: 0, idle: 1
