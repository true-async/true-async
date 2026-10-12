--TEST--
Pool: the check replaces a resource it destroyed, up to min
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
        $made++;
        if ($made === 3) {
            $refilled->complete(null);
        }
        return $made;
    },
    destructor: function ($resource) {
        echo "destroyed: $resource\n";
    },
    healthcheck: fn($resource) => $resource !== 1,
    min: 2,
    max: 4,
    healthcheckInterval: 1,
);

$keeper = spawn(fn() => delay(10000));
await(new Async\Future($refilled));
$keeper->cancel();
echo "count: ", count($pool), ", idle: ", $pool->idleCount(), "\n";
$pool->close();
?>
--EXPECT--
destroyed: 1
count: 2, idle: 2
destroyed: 2
destroyed: 3
