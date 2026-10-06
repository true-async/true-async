--TEST--
await() on a Future: the parked coroutine's awaiting info names the future
--FILE--
<?php

use Async\FutureState;
use Async\Future;
use function Async\spawn;
use function Async\await;
use function Async\suspend;

$state = new FutureState();
$future = new Future($state);

$waiter = spawn(fn() => await($future));
suspend();

var_dump($waiter->getAwaitingInfo());

$state->complete("value");
echo await($waiter), "\n";

?>
--EXPECT--
array(1) {
  [0]=>
  string(13) "await: future"
}
value
