--TEST--
Pool: a destructor's exception when the check's release closes the pool is printed at the request's end and cancels nobody
--FILE--
<?php

use Async\Pool;
use function Async\await;
use function Async\delay;
use function Async\spawn;

$entered = new Async\FutureState();
$gate = new Async\FutureState();
$gateFuture = new Async\Future($gate);
$checkDone = new Async\FutureState();

$pool = new Pool(
    factory: fn() => "resource",
    destructor: function ($resource) use ($checkDone) {
        $checkDone->complete(null);
        throw new RuntimeException("quit failed");
    },
    healthcheck: function ($resource) use ($entered, $gateFuture) {
        $entered->complete(null);
        await($gateFuture);
        return true;
    },
    min: 1,
    healthcheckInterval: 1,
);

$other = spawn(function () {
    try {
        delay(10000);
        echo "other: not cancelled\n";
    } catch (Async\AsyncCancellation $cancellation) {
        echo "other: ", $cancellation->getMessage(), "\n";
    }
});
await(new Async\Future($entered));
$pool = null;
$gate->complete(null);
await(new Async\Future($checkDone));
Async\suspend();
echo "running: ", $other->isCompleted() ? "no" : "yes", "\n";
$other->cancel();
?>
--EXPECTF--
running: yes
other: Coroutine cancelled
%AFatal error: Uncaught RuntimeException: quit failed in %s:%d
%A
