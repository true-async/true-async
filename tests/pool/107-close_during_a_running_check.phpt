--TEST--
Pool: close() during a running check cancels it, and the resource it holds is destroyed when the check returns
--FILE--
<?php

use Async\Pool;
use function Async\await;
use function Async\delay;
use function Async\spawn;

$entered = new Async\FutureState();

$pool = new Pool(
    factory: fn() => "resource",
    destructor: function ($resource) {
        echo "destroyed: $resource\n";
    },
    healthcheck: function ($resource) use ($entered) {
        $entered->complete(null);
        try {
            delay(10000);
        } catch (Async\AsyncCancellation $cancellation) {
            echo "check: ", $cancellation->getMessage(), "\n";
            throw $cancellation;
        }
        return true;
    },
    min: 1,
    healthcheckInterval: 1,
);

// The check runs on a timer that keeps nothing alive: the keeper keeps the loop running until it starts.
$keeper = Async\spawn(fn() => Async\delay(10000));
await(new Async\Future($entered));
$keeper->cancel();
echo "before close: count ", count($pool), ", idle ", $pool->idleCount(), "\n";
$pool->close();
echo "after close: count ", count($pool), "\n";
Async\suspend();
echo "after the check: count ", count($pool), "\n";
?>
--EXPECT--
before close: count 1, idle 0
after close: count 1
check: Coroutine cancelled
destroyed: resource
after the check: count 0
