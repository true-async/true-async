--TEST--
TaskSet: dispose() - cancels scope coroutines
--FILE--
<?php

use Async\TaskSet;
use function Async\spawn;
use function Async\suspend;

spawn(function() {
    $set = new TaskSet();

    $state = null;

    $set->spawn(function() use (&$state) {
        $state = "started";
        try {
            suspend();
            suspend();
            $state = "finished";
            return "should be cancelled";
        } catch (Async\AsyncCancellation) {
            $state = "cancelled";
        }
    });

    // A task the dispose finds unstarted would never run its body.
    while ($state === null) {
        suspend();
    }

    $set->dispose();

    echo "disposed\n";
    var_dump($set->isFinished());

    while ($state === "started") {
        suspend();
    }
    echo "task $state\n";
});
?>
--EXPECT--
disposed
bool(false)
task cancelled
