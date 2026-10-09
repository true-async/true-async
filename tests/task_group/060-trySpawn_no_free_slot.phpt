--TEST--
TaskGroup: trySpawn() with no free slot returns false and queues nothing
--FILE--
<?php

use Async\Future;
use Async\FutureState;
use Async\TaskGroup;

$gate = new FutureState();
$group = new TaskGroup(concurrency: 1);
$group->spawn(fn() => (new Future($gate))->await());

var_dump($group->trySpawn(function () {
    echo "refused task ran\n";
}));
var_dump(count($group));

$gate->complete("first");
var_dump($group->all()->await());
?>
--EXPECT--
bool(false)
int(1)
array(1) {
  [0]=>
  string(5) "first"
}
