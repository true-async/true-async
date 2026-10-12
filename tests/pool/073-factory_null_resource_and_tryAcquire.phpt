--TEST--
Pool: a factory returning null hands out null, which tryAcquire() cannot tell from no resource
--FILE--
<?php

$pool = new Async\Pool(factory: fn() => null, max: 1);

var_dump($pool->tryAcquire());
printf("active=%d\n", $pool->activeCount());

var_dump($pool->tryAcquire());
printf("active=%d\n", $pool->activeCount());

$pool->release(null);
printf("idle=%d active=%d\n", $pool->idleCount(), $pool->activeCount());
?>
--EXPECT--
NULL
active=1
NULL
active=1
idle=1 active=0
