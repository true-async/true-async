--TEST--
TaskGroup: getAwaitingInfo() of a coroutine parked in spawn() on a full queue names the group's wait
--FILE--
<?php

use Async\Future;
use Async\FutureState;
use Async\TaskGroup;
use function Async\spawn;
use function Async\suspend;

spawn(function () {
    $group = new TaskGroup(concurrency: 1, queueLimit: 1);
    $group->spawn(fn() => (new Future(new FutureState()))->await());
    $group->spawn(fn() => "queued");
    $spawner = spawn(function () use ($group) {
        try {
            $group->spawn(fn() => "third");
        } catch (Async\AsyncException $exception) {
            echo $exception->getMessage(), "\n";
        }
    });

    suspend();
    var_dump($spawner->getAwaitingInfo());
    $group->cancel();
});
?>
--EXPECT--
array(1) {
  [0]=>
  string(63) "TaskGroup(total=2, active=1, queued=1): spawn() on a full queue"
}
Cannot spawn tasks on a closed TaskGroup
