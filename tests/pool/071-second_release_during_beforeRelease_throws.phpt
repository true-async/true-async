--TEST--
Pool: a second release() of an object while the first waits in beforeRelease throws
--FILE--
<?php

use Async\Pool;
use Async\PoolException;
use function Async\spawn;
use function Async\await;
use function Async\suspend;

$inHook = false;
$leave = false;

$pool = new Pool(
    factory: fn() => new stdClass(),
    beforeRelease: function ($resource) use (&$inHook, &$leave) {
        $inHook = true;
        while (!$leave) {
            suspend();
        }
        return true;
    },
    max: 2,
);

$resource = $pool->tryAcquire();

$first = spawn(fn() => $pool->release($resource));

while (!$inHook) {
    suspend();
}

try {
    $pool->release($resource);
    echo "second release: returned\n";
} catch (PoolException $e) {
    echo "second release: ", $e->getMessage(), "\n";
}

$leave = true;
await($first);

printf("idle=%d active=%d\n", $pool->idleCount(), $pool->activeCount());
?>
--EXPECT--
second release: The resource was not acquired from this pool, or was released already
idle=1 active=0
