--TEST--
TaskGroup: dispose() - cancels scope coroutines
--FILE--
<?php

use Async\TaskGroup;
use function Async\spawn;
use function Async\suspend;

spawn(function() {
    $group = new TaskGroup();

    $state = null;

    $group->spawn(function() use (&$state) {
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

    $group->dispose();
    $group->suppressErrors();

    echo "disposed\n";
    var_dump($group->isFinished());

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
