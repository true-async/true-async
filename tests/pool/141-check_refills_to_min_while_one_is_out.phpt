--TEST--
Pool: with a resource handed out, a check that destroys the idle one makes a new one up to min
--FILE--
<?php

use Async\Pool;
use function Async\await;
use function Async\delay;
use function Async\spawn;

$made = 0;
$refilled = new Async\FutureState();

$pool = new Pool(
    factory: function () use (&$made, $refilled) {
        if (++$made === 3) {
            $refilled->complete(null);
        }
        return $made;
    },
    healthcheck: fn($resource) => false,
    min: 2,
    healthcheckInterval: 1,
);

$held = $pool->acquire();
$keeper = spawn(fn() => delay(10000));
await(new Async\Future($refilled));
$keeper->cancel();

echo "held: $held, count: ", $pool->count(), "\n";
$pool->close();
?>
--EXPECT--
held: 1, count: 2
