--TEST--
TaskGroup: a settled open group takes new tasks, and all() then waits for them too
--FILE--
<?php

use Async\TaskGroup;

$group = new TaskGroup();
$group->spawn(fn() => "first");
var_dump($group->all()->await());
var_dump($group->isFinished());

$group->spawn(fn() => "second");
var_dump($group->isFinished());
var_dump($group->all()->await());
?>
--EXPECT--
array(1) {
  [0]=>
  string(5) "first"
}
bool(true)
bool(false)
array(2) {
  [0]=>
  string(5) "first"
  [1]=>
  string(6) "second"
}
