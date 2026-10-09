--TEST--
TaskGroup: awaitCompletion() - waits after cancel
--XFAIL--
Not implemented yet: S9.29 of dev/PLAN.md
--FILE--
<?php

use Async\TaskGroup;
use function Async\spawn;
use function Async\suspend;

spawn(function() {
    $group = new TaskGroup();

    $group->spawn(function() {
        suspend();
        return "should be cancelled";
    });

    $group->spawn(function() { return "fast"; });

    $group->cancel();
    $group->awaitCompletion();

    echo "completed after cancel\n";
    var_dump($group->isFinished());
    var_dump($group->isClosed());
});
?>
--EXPECT--
completed after cancel
bool(true)
bool(true)
