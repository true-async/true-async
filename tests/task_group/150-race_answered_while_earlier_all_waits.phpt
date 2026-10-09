--TEST--
TaskGroup: a race() made after a pending all() is answered at the first task end, not at the completion
--FILE--
<?php

use Async\Future;
use Async\FutureState;
use Async\TaskGroup;

$gate = new FutureState();
$group = new TaskGroup();
$group->spawn(fn() => "first");
$group->spawn(fn() => (new Future($gate))->await());
$all = $group->all();
$race = $group->race();

echo $race->await(), "\n";
$gate->complete("second");
$group->close();
var_dump($all->await());
?>
--EXPECT--
first
array(2) {
  [0]=>
  string(5) "first"
  [1]=>
  string(6) "second"
}
