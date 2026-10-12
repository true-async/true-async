--TEST--
Pool: a place a beforeAcquire rejection frees goes to a parked waiter when the circuit breaker then refuses the caller
--FILE--
<?php

use Async\Pool;
use Async\ServiceUnavailableException;
use function Async\await;
use function Async\spawn;
use function Async\suspend;

$entered = new Async\FutureState();
$gate = new Async\FutureState();
$gateFuture = new Async\Future($gate);

$pool = new Pool(
    factory: fn() => "resource",
    beforeAcquire: function ($resource) use ($entered, $gateFuture) {
        $entered->complete(null);
        await($gateFuture);
        return false;
    },
    min: 1,
    max: 1,
);

$caller = spawn(function () use ($pool) {
    try {
        $pool->acquire();
    } catch (ServiceUnavailableException $exception) {
        echo "caller: ", $exception->getMessage(), "\n";
    }
});
await(new Async\Future($entered));

$parked = false;
$waiter = spawn(function () use ($pool, &$parked) {
    $parked = true;
    try {
        $pool->acquire();
    } catch (ServiceUnavailableException $exception) {
        echo "waiter: ", $exception->getMessage(), "\n";
    }
});

while (!$parked) {
    suspend();
}

$pool->deactivate();
$gate->complete(null);
await($caller);
await($waiter);
?>
--EXPECT--
caller: Service is unavailable (circuit breaker is open)
waiter: Service is unavailable (circuit breaker is open)
