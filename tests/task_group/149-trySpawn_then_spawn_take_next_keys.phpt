--TEST--
TaskGroup: a spawn() after a successful trySpawn() takes the next integer key
--FILE--
<?php

use Async\TaskGroup;

$group = new TaskGroup(concurrency: 2);
$group->trySpawn(fn() => "tried");
$group->spawn(fn() => "spawned");
$group->close();
var_dump($group->all()->await());
?>
--EXPECT--
array(2) {
  [0]=>
  string(5) "tried"
  [1]=>
  string(7) "spawned"
}
