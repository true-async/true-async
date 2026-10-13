--TEST--
getAwaitingInfo() of a waiter names a Coroutine token
--FILE--
<?php
use Async\Future;
use Async\FutureState;
use function Async\spawn;
use function Async\await;
use function Async\suspend;

$token = spawn(function () {
    suspend();
    suspend();
});

$state = new FutureState();
$waiter = spawn(fn() => await(new Future($state), $token));
suspend();
print_r($waiter->getAwaitingInfo());
$state->complete(1);
var_dump(await($waiter));
?>
--EXPECTF--
Array
(
    [0] => await: future
    [1] => cancellation: coroutine #%d
)
int(1)
