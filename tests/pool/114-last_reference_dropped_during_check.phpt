--TEST--
Pool: the pool's last reference dropped during a check: the check holds it, and the pool closes as the check ends
--FILE--
<?php

use Async\Pool;
use function Async\await;

$entered = new Async\FutureState();
$gate = new Async\FutureState();
$gateFuture = new Async\Future($gate);

$pool = new Pool(
    factory: fn() => "resource",
    destructor: function ($resource) {
        echo "destroyed: $resource in ", Async\current_coroutine() === $GLOBALS['main'] ? "main" : "the check", "\n";
    },
    healthcheck: function ($resource) use ($entered, $gateFuture) {
        $entered->complete(null);
        await($gateFuture);
        return true;
    },
    min: 1,
    healthcheckInterval: 1,
);

$main = Async\current_coroutine();
// The check runs on a timer that keeps nothing alive: the keeper keeps the loop running until it starts.
$keeper = Async\spawn(fn() => Async\delay(10000));
await(new Async\Future($entered));
$keeper->cancel();
$pool = null;
echo "dropped\n";
$gate->complete(null);
Async\suspend();
echo "end\n";
?>
--EXPECT--
dropped
destroyed: resource in the check
end
