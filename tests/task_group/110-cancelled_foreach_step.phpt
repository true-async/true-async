--TEST--
TaskGroup: cancelling a coroutine parked in a foreach step throws the cancellation out of the loop and leaves the group usable
--FILE--
<?php

use Async\Future;
use Async\FutureState;
use Async\TaskGroup;
use function Async\await;
use function Async\spawn;
use function Async\suspend;

spawn(function () {
    $gate = new FutureState();
    $group = new TaskGroup();
    $group->spawn(function () use ($gate) {
        (new Future($gate))->await();

        return "done";
    });
    $consumer = spawn(function () use ($group) {
        try {
            foreach ($group as $key => [$result, $error]) {
                echo "consumer got $key\n";
            }
        } catch (Async\AsyncCancellation $cancellation) {
            echo "consumer: ", $cancellation->getMessage(), "\n";
        }
    });

    suspend();
    $consumer->cancel(new Async\AsyncCancellation("stop"));
    await($consumer);

    $gate->complete(null);
    $group->close();
    $group->awaitCompletion();
    var_dump($group->getResults());
});
?>
--EXPECT--
consumer: stop
array(1) {
  [0]=>
  string(4) "done"
}
