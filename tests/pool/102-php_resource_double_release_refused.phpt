--TEST--
Pool: a PHP resource released twice is refused the second time
--FILE--
<?php

$pool = new Async\Pool(factory: fn() => fopen('php://memory', 'r'));

$stream = $pool->acquire();
$pool->release($stream);

try {
    $pool->release($stream);
} catch (Async\PoolException $e) {
    echo $e->getMessage(), "\n";
}

echo "idle: ", $pool->idleCount(), "\n";
?>
--EXPECT--
The resource was not acquired from this pool, or was released already
idle: 1
