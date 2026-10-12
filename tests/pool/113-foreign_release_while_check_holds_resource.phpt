--TEST--
Pool: a release of a foreign object while the check holds the only resource is refused and changes no count
--FILE--
<?php

use Async\Pool;
use function Async\await;

$entered = new Async\FutureState();
$gate = new Async\FutureState();
$gateFuture = new Async\Future($gate);

$pool = new Pool(
    factory: fn() => new stdClass(),
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

try {
    $pool->release(new stdClass());
} catch (Async\PoolException $exception) {
    echo "release: ", $exception->getMessage(), "\n";
}

echo "count: ", count($pool), ", active: ", $pool->activeCount(), "\n";
$gate->complete(null);
Async\suspend();
echo "idle: ", $pool->idleCount(), "\n";
$pool->close();
?>
--EXPECT--
release: The resource was not acquired from this pool, or was released already
count: 1, active: 1
idle: 1
