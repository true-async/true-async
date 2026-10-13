--TEST--
Pool: close() while the check's refill factory runs destroys the resource it still returns
--FILE--
<?php

use Async\Pool;
use function Async\await;
use function Async\delay;
use function Async\spawn;

$refilling = new Async\FutureState();
$refillDestroyed = new Async\FutureState();
$made = 0;

$pool = new Pool(
    factory: function () use (&$made, $refilling) {
        if (++$made === 2) {
            $refilling->complete(null);
            try {
                delay(10000);
            } catch (Async\AsyncCancellation) {
                echo "factory cancelled\n";
            }
        }
        return $made;
    },
    destructor: function ($resource) use ($refillDestroyed) {
        echo "destroyed $resource\n";
        if ($resource === 2) {
            $refillDestroyed->complete(null);
        }
    },
    healthcheck: fn($resource) => false,
    min: 1,
    healthcheckInterval: 1,
);

$keeper = spawn(fn() => delay(10000));
await(new Async\Future($refilling));
$pool->close();
await(new Async\Future($refillDestroyed));
$keeper->cancel();
echo "count: ", $pool->count(), "\n";
?>
--EXPECT--
destroyed 1
factory cancelled
destroyed 2
count: 0
