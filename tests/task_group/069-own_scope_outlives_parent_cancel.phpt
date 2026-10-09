--TEST--
TaskGroup: the group keeps its results after the cancel of its own scope's idle parent
--FILE--
<?php

use Async\Scope;
use Async\TaskGroup;

$parent = new Scope();
$group = $parent->spawn(function () {
    $group = new TaskGroup();
    $group->spawn(fn() => "done");
    $group->all()->await();

    return $group;
});
$group = Async\await($group);

$parent->cancel();
var_dump($group->getResults());
?>
--EXPECT--
array(1) {
  [0]=>
  string(4) "done"
}
