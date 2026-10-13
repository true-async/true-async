--TEST--
Pool: a PHP resource the factory hands out twice can be released twice, and not a third time
--FILE--
<?php

use Async\Pool;

$handle = fopen('php://memory', 'r');
$pool = new Pool(factory: fn() => $handle, max: 2);

$first = $pool->acquire();
$second = $pool->acquire();
$pool->release($first);
$pool->release($second);
echo "released twice\n";

try {
    $pool->release($handle);
} catch (Async\PoolException $exception) {
    echo $exception->getMessage(), "\n";
}
?>
--EXPECT--
released twice
The resource was not acquired from this pool, or was released already
