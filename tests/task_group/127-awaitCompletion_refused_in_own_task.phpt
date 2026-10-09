--TEST--
TaskGroup: awaitCompletion() in one of the group's own tasks throws instead of waiting for itself
--FILE--
<?php

use Async\TaskGroup;

$group = new TaskGroup();
$group->spawn(function () use ($group) {
    try {
        $group->awaitCompletion();
    } catch (Async\AsyncException $exception) {
        echo get_class($exception), ": ", $exception->getMessage(), "\n";
    }
});
$group->close();
$group->awaitCompletion();
echo "count: ", count($group), "\n";
?>
--EXPECT--
Async\AsyncException: Cannot await completion of TaskGroup from one of its tasks
count: 1
