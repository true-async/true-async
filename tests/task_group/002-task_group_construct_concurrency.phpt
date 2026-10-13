--TEST--
TaskGroup: __construct() - with concurrency limit
--FILE--
<?php

use Async\TaskGroup;
use function Async\spawn;
use function Async\await;
use function Async\suspend;

spawn(function() {
    $group = new TaskGroup(2);

    $order = [];

    $group->spawn(function() use (&$order) {
        $order[] = "task1-start";
        suspend();
        $order[] = "task1-end";
        return "r1";
    });

    $group->spawn(function() use (&$order) {
        $order[] = "task2-start";
        suspend();
        $order[] = "task2-end";
        return "r2";
    });

    $group->spawn(function() use (&$order) {
        $order[] = "task3-start";
        return "r3";
    });

    $group->close();
    $results = $group->all()->await();

    foreach ($order as $item) {
        echo "$item\n";
    }

    echo "count: " . count($results) . "\n";
    echo "done\n";
});
?>
--EXPECT--
task1-start
task2-start
task1-end
task2-end
task3-start
count: 3
done
