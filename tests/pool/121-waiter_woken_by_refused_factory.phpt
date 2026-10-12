--TEST--
Pool: a waiter that parked while the factory ran is woken when the factory throws, and makes the resource itself
--FILE--
<?php

use Async\Pool;
use function Async\await;
use function Async\spawn;
use function Async\suspend;

$entered = new Async\FutureState();
$gate = new Async\FutureState();
$gateFuture = new Async\Future($gate);
$calls = 0;

$pool = new Pool(
    factory: function () use (&$calls, $entered, $gateFuture) {
        if (++$calls === 1) {
            $entered->complete(null);
            await($gateFuture);
            throw new RuntimeException("connect failed");
        }
        return "resource $calls";
    },
    max: 1,
);

$caller = spawn(function () use ($pool) {
    try {
        $pool->acquire();
    } catch (RuntimeException $exception) {
        echo "caller: ", $exception->getMessage(), "\n";
    }
});
await(new Async\Future($entered));

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
await($caller);
await($waiter);
?>
--EXPECT--
caller: connect failed
waiter: resource 2
