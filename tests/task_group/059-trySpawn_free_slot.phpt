--TEST--
TaskGroup: trySpawn() with a free slot starts the task and returns true
--FILE--
<?php

use Async\TaskGroup;

$group = new TaskGroup(concurrency: 1);
var_dump($group->trySpawn(fn() => "done"));
var_dump($group->all()->await());
?>
--EXPECT--
bool(true)
array(1) {
  [0]=>
  string(4) "done"
}
