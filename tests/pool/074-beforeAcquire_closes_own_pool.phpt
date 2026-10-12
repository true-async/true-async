--TEST--
Pool: beforeAcquire that closes its own pool: the resource is destroyed and acquire() finds the pool closed
--FILE--
<?php

use Async\Pool;
use Async\PoolException;

$pool = new Pool(
    factory: fn() => 'r',
    destructor: function ($resource) {
        echo "destroyed $resource\n";
    },
    beforeAcquire: function ($resource) use (&$pool) {
        $pool->close();
        return true;
    },
    min: 1,
);

try {
    $pool->acquire();
    echo "acquired\n";
} catch (PoolException $e) {
    echo $e->getMessage(), "\n";
}

printf("count=%d\n", count($pool));
?>
--EXPECT--
destroyed r
Pool is closed
count=0
