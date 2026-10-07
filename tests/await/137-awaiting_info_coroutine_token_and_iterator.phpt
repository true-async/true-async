--TEST--
getAwaitingInfo() of a waiter names a Coroutine token, and the iterator coroutine of an await_all() over a generator
--FILE--
<?php
use Async\Future;
use Async\FutureState;
use function Async\spawn;
use function Async\await;
use function Async\await_all;
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

$state = new FutureState();
$waiter = spawn(fn() => await_all((function () use ($state) {
    yield new Future($state);
})()));
suspend();
print_r($waiter->getAwaitingInfo());
$state->complete(2);
var_dump(await($waiter)[0]);
?>
--EXPECTF--
Array
(
    [0] => await: future
    [1] => cancellation: coroutine #%d
)
int(1)
Array
(
    [0] => await: iterator coroutine #%d
)
array(1) {
  [0]=>
  int(2)
}
