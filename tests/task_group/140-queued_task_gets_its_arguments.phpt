--TEST--
TaskGroup: a queued task starts with the positional and named arguments given to spawn()
--FILE--
<?php

use Async\Future;
use Async\FutureState;
use Async\TaskGroup;

$gate = new FutureState();
$group = new TaskGroup(concurrency: 1);
$group->spawn(fn() => (new Future($gate))->await());
$group->spawn(function (string $first, string $second, string $third = "unset") {
    return "$first $second $third";
}, str_repeat("a", 2), str_repeat("b", 2), third: "c");

$gate->complete("gate");
var_dump($group->all()->await());
?>
--EXPECT--
array(2) {
  [0]=>
  string(4) "gate"
  [1]=>
  string(7) "aa bb c"
}
