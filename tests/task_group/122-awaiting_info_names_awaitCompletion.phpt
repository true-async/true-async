--TEST--
TaskSet: getAwaitingInfo() of a coroutine parked in awaitCompletion() names the set's wait
--FILE--
<?php

use Async\Future;
use Async\FutureState;
use Async\TaskSet;
use function Async\spawn;
use function Async\suspend;

spawn(function () {
    $set = new TaskSet();
    $set->spawn(fn() => (new Future(new FutureState()))->await());
    $set->close();
    $waiter = spawn(fn() => $set->awaitCompletion());

    suspend();
    var_dump($waiter->getAwaitingInfo());
    $set->cancel();
});
?>
--EXPECT--
array(1) {
  [0]=>
  string(55) "TaskSet(total=1, active=1, queued=0): awaitCompletion()"
}
