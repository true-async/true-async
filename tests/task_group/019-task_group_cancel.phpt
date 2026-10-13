--TEST--
TaskGroup: cancel() - stops running tasks and clears queue
--FILE--
<?php

use Async\TaskGroup;
use function Async\spawn;
use function Async\suspend;

spawn(function() {
    $group = new TaskGroup(1);

    $started = false;

    $group->spawn(function() use (&$started) {
        $started = true;
        try {
            suspend();
            suspend();
            return "should not finish";
        } finally {
            echo "running task cancelled\n";
        }
    });

    $group->spawn(function() {
        echo "queued task must not start\n";
        return "queued - never started";
    });

    // A task the cancel finds unstarted would never run its body.
    while (!$started) {
        suspend();
    }

    $group->cancel();

    var_dump($group->isClosed());
    var_dump($group->isFinished());
    $group->awaitCompletion();
    var_dump($group->isFinished());
    echo "results: " . count($group->getResults()) . "\n";
    foreach ($group->getErrors() as $error) {
        echo get_class($error), "\n";
    }
    echo "done\n";
});
?>
--EXPECT--
bool(true)
bool(false)
running task cancelled
bool(true)
results: 0
Async\AsyncCancellation
Async\AsyncCancellation
done
