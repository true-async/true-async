--TEST--
Pool: a resource the check destroys frees a place a parked waiter takes
--FILE--
<?php

use Async\Pool;
use function Async\await;
use function Async\spawn;
use function Async\suspend;

$entered = new Async\FutureState();
$gate = new Async\FutureState();
$gateFuture = new Async\Future($gate);
$made = 0;

$pool = new Pool(
    factory: function () use (&$made) {
        return ++$made;
    },
    healthcheck: function ($resource) use ($entered, $gateFuture) {
        if (!$entered->isCompleted()) {
            $entered->complete(null);
        }
        await($gateFuture);
        return false;
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

$gate->complete(null);
await($waiter);
$pool->close();
?>
--EXPECT--
waiter: 2
