--TEST--
Pool: the destructor's exception for a resource beforeAcquire rejects reaches acquire()
--FILE--
<?php

use Async\Pool;

$made = 0;
$pool = new Pool(
    factory: function () use (&$made) {
        return ++$made;
    },
    destructor: function ($resource) {
        throw new RuntimeException("destructor of $resource");
    },
    beforeAcquire: fn($resource) => false,
);
$pool->release($pool->acquire());

try {
    $pool->acquire();
} catch (RuntimeException $exception) {
    echo $exception->getMessage(), "\n";
}

echo "count: ", $pool->count(), ", active: ", $pool->activeCount(), "\n";
?>
--EXPECT--
destructor of 1
count: 0, active: 0
