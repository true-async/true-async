--TEST--
TaskGroup: a second foreach over a TaskGroup yields every settled entry again
--FILE--
<?php

use Async\TaskGroup;
use function Async\spawn;

spawn(function () {
    $group = new TaskGroup();
    $group->spawnWithKey("a", fn() => 1);
    $group->spawnWithKey("b", fn() => 2);
    $group->close();

    foreach ($group as $key => [$result, $error]) {
        echo "first loop: $key => $result\n";
    }

    foreach ($group as $key => [$result, $error]) {
        echo "second loop: $key => $result\n";
    }
});
?>
--EXPECT--
first loop: a => 1
first loop: b => 2
second loop: a => 1
second loop: b => 2
