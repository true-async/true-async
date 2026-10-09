--TEST--
TaskGroup: trySpawn() whose next integer key spawnWithKey() took throws once, and the next call takes the integer after it
--FILE--
<?php

use Async\TaskGroup;

$group = new TaskGroup();
$group->spawnWithKey(0, fn() => "keyed");

try {
    $group->trySpawn(fn() => "lost");
} catch (Async\AsyncException $error) {
    echo $error->getMessage(), "\n";
}

var_dump($group->trySpawn(fn() => "second"));
var_dump($group->all()->await());
?>
--EXPECT--
Duplicate key 0 in TaskGroup
bool(true)
array(2) {
  [0]=>
  string(5) "keyed"
  [1]=>
  string(6) "second"
}
