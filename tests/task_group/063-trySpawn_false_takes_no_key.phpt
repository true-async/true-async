--TEST--
TaskGroup: a trySpawn() that returns false takes no integer key
--FILE--
<?php

use Async\Future;
use Async\FutureState;
use Async\TaskGroup;

$gate = new FutureState();
$group = new TaskGroup(concurrency: 1);
$group->spawn(fn() => (new Future($gate))->await());
var_dump($group->trySpawn(fn() => "refused"));

$gate->complete("first");
$group->all()->await();
$group->spawn(fn() => "second");

var_dump($group->all()->await());
?>
--EXPECT--
bool(false)
array(2) {
  [0]=>
  string(5) "first"
  [1]=>
  string(6) "second"
}
