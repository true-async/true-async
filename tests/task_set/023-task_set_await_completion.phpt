--TEST--
TaskSet: awaitCompletion() - waits for all tasks to settle
--XFAIL--
Not implemented yet: S9.29 of dev/PLAN.md
--FILE--
<?php

use Async\TaskSet;
use function Async\spawn;

spawn(function() {
    $set = new TaskSet();

    $set->spawn(function() { return "a"; });
    $set->spawn(function() { return "b"; });

    $set->close();
    $set->awaitCompletion();

    echo "completed\n";
    var_dump($set->isFinished());
});
?>
--EXPECT--
completed
bool(true)
