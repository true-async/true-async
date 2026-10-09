--TEST--
TaskGroup: cancelling a coroutine parked in awaitCompletion() throws the cancellation, and a later awaitCompletion() waits again
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
    $group->spawn(fn() => (new Future($gate))->await());
    $group->close();
    $waiter = spawn(function () use ($group) {
        try {
            $group->awaitCompletion();
            echo "waiter: completed\n";
        } catch (Async\AsyncCancellation $cancellation) {
            echo "waiter: ", $cancellation->getMessage(), "\n";
        }
    });

    suspend();
    $waiter->cancel(new Async\AsyncCancellation("stop"));
    await($waiter);

    $gate->complete(null);
    $group->awaitCompletion();
    echo "completed\n";
});
?>
--EXPECT--
waiter: stop
completed
