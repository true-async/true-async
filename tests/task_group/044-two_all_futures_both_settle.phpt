--TEST--
TaskGroup: two all() Futures pending when the group settles both resolve
--FILE--
<?php

use Async\Future;
use Async\FutureState;
use Async\TaskGroup;

$gate = new FutureState();
$group = new TaskGroup();
$group->spawn(fn() => (new Future($gate))->await());

$first = $group->all();
$second = $group->all();
$gate->complete(1);

var_dump($first->await());
var_dump($second->await());
?>
--EXPECT--
array(1) {
  [0]=>
  int(1)
}
array(1) {
  [0]=>
  int(1)
}
