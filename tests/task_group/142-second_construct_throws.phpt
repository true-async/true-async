--TEST--
TaskGroup: a second __construct() throws Error and leaves the group as it was
--FILE--
<?php

$group = new Async\TaskGroup(concurrency: 1);

try {
    $group->__construct(2);
} catch (Error $e) {
    echo get_class($e), ": ", $e->getMessage(), "\n";
}

echo $group->trySpawn(fn() => 1) ? "first started\n" : "first refused\n";
echo $group->trySpawn(fn() => 2) ? "second started\n" : "second refused\n";
?>
--EXPECT--
Error: Cannot call constructor twice
first started
second refused
