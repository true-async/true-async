--TEST--
TaskGroup: a foreach in one of the group's own tasks is not refused and may stop early
--FILE--
<?php

use Async\TaskGroup;

$group = new TaskGroup();
$group->spawn(fn() => "first");
$group->spawn(function () use ($group) {
    foreach ($group as $key => [$result, $error]) {
        echo "own task got $key => $result\n";
        break;
    }

    return "second";
});
$group->close();
$group->awaitCompletion();
echo "count: ", count($group), "\n";
?>
--EXPECT--
own task got 0 => first
count: 2
