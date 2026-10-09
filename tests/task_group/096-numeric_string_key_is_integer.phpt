--TEST--
TaskGroup: a numeric string key is the integer key, as in an array
--FILE--
<?php

use Async\TaskGroup;

$group = new TaskGroup();
$group->spawnWithKey("1", fn() => "first");

try {
    $group->spawnWithKey(1, fn() => "second");
} catch (Async\AsyncException $error) {
    echo $error->getMessage(), "\n";
}

var_dump($group->all()->await());
?>
--EXPECT--
Duplicate key 1 in TaskGroup
array(1) {
  [1]=>
  string(5) "first"
}
