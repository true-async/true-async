--TEST--
Pool: a waiter that parks while the check holds the only resource is woken when the check returns it
--FILE--
<?php

use Async\Pool;
use function Async\await;
use function Async\spawn;
use function Async\suspend;

$entered = new Async\FutureState();
$gate = new Async\FutureState();
$gateFuture = new Async\Future($gate);

$pool = new Pool(
    factory: fn() => "resource",
    healthcheck: function ($resource) use ($entered, $gateFuture) {
        $entered->complete(null);
        await($gateFuture);
        return true;
    },
    min: 1,
    max: 1,
    healthcheckInterval: 1,
);

// The check runs on a timer that keeps nothing alive: the keeper keeps the loop running until it starts.
$keeper = Async\spawn(fn() => Async\delay(10000));
await(new Async\Future($entered));
$keeper->cancel();
$parked = false;
$waiter = spawn(function () use ($pool, &$parked) {
    $parked = true;
    $resource = $pool->acquire();
    echo "waiter: $resource\n";
});

while (!$parked) {
    suspend();
}

echo "while checked: count ", count($pool), ", idle ", $pool->idleCount(), "\n";
$gate->complete(null);
await($waiter);
$pool->close();
?>
--EXPECT--
while checked: count 1, idle 0
waiter: resource
