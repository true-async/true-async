--TEST--
TaskGroup: awaitCompletion() - waits for all tasks to settle
--XFAIL--
Not implemented yet: S9.29 of dev/PLAN.md
--FILE--
<?php

use Async\TaskGroup;
use function Async\spawn;

spawn(function() {
    $group = new TaskGroup();

    $group->spawn(function() { return "a"; });
    $group->spawn(function() { return "b"; });

    $group->close();
    $group->awaitCompletion();

    echo "completed\n";
    var_dump($group->isFinished());

    $results = $group->getResults();
    var_dump($results[0]);
    var_dump($results[1]);
});
?>
--EXPECT--
completed
bool(true)
string(1) "a"
string(1) "b"
