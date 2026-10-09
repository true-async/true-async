--TEST--
TaskGroup: spawn() after dispose() throws, since dispose() seals the group
--FILE--
<?php

use Async\TaskGroup;

$group = new TaskGroup();
$group->dispose();

try {
    $group->spawn(fn() => 1);
} catch (Async\AsyncException $error) {
    echo $error->getMessage(), "\n";
}

var_dump($group->isClosed(), count($group));
?>
--EXPECT--
Cannot spawn tasks on a closed TaskGroup
bool(true)
int(0)
