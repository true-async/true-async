--TEST--
TaskGroup: getAwaitingInfo() of a coroutine parked in a foreach step names the group's wait
--FILE--
<?php

use Async\Future;
use Async\FutureState;
use Async\TaskGroup;
use function Async\spawn;
use function Async\suspend;

spawn(function () {
    $group = new TaskGroup();
    $group->spawn(fn() => (new Future(new FutureState()))->await());
    $consumer = spawn(function () use ($group) {
        try {
            foreach ($group as $pair) {
            }
        } catch (Async\AsyncCancellation) {
        }
    });

    suspend();
    var_dump($consumer->getAwaitingInfo());
    $consumer->cancel();
    $group->cancel();
});
?>
--EXPECT--
array(1) {
  [0]=>
  string(47) "TaskGroup(total=1, active=1, queued=0): foreach"
}
