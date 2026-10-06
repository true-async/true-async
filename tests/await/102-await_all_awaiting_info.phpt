--TEST--
getAwaitingInfo() of a coroutine parked in await_all(): one line per pending trigger and one for the token
--FILE--
<?php

use Async\FutureState;
use Async\Future;
use function Async\spawn;
use function Async\await_all;
use function Async\suspend;

$state = new FutureState();
$future = new Future($state);
$tokenState = new FutureState();
$token = new Future($tokenState);

$target = spawn(function () {
    suspend();
    suspend();
    return "target";
});

$waiter = spawn(function () use ($target, $future, $token) {
    return await_all([$target, $future], $token);
});

suspend();

foreach ($waiter->getAwaitingInfo() as $line) {
    echo preg_replace('/#\d+/', '#N', $line), "\n";
}

$state->complete("future");
[$results, $errors] = Async\await($waiter);
var_dump($results, $errors);

?>
--EXPECT--
cancellation: future
await: coroutine #N
await: future
array(2) {
  [0]=>
  string(6) "target"
  [1]=>
  string(6) "future"
}
array(0) {
}
