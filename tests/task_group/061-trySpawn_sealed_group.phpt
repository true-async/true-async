--TEST--
TaskGroup: trySpawn() on a sealed group throws, as spawn() does
--FILE--
<?php

use Async\TaskGroup;

$group = new TaskGroup();
$group->close();

try {
    $group->trySpawn(fn() => 1);
} catch (Async\AsyncException $error) {
    echo $error->getMessage(), "\n";
}
?>
--EXPECT--
Cannot spawn tasks on a closed TaskGroup
