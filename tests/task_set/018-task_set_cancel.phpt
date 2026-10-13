--TEST--
TaskSet: cancel() - stops running tasks
--FILE--
<?php

use Async\TaskSet;
use function Async\spawn;
use function Async\suspend;

spawn(function() {
    $set = new TaskSet(1);

    $started = false;

    $set->spawn(function() use (&$started) {
        $started = true;
        try {
            suspend();
            suspend();
            return "should not finish";
        } catch (Async\AsyncCancellation $e) {
            echo "running task cancelled\n";
            throw $e;
        }
    });

    $set->spawn(function() {
        echo "queued task must not start\n";
        return "queued - never started";
    });

    // A task the cancel finds unstarted would never run its body.
    while (!$started) {
        suspend();
    }

    $set->cancel();

    var_dump($set->isClosed());
    var_dump($set->isFinished());
    $set->awaitCompletion();
    var_dump($set->isFinished());
    echo "done\n";
});
?>
--EXPECT--
bool(true)
bool(false)
running task cancelled
bool(true)
done
