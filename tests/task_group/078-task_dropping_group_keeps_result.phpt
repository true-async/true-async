--TEST--
TaskGroup: a task whose closure releases the group's last reference keeps its result
--FILE--
<?php

use Async\Future;
use Async\FutureState;
use Async\TaskGroup;

$closed = new FutureState();
$group = new TaskGroup();
$group->finally(function (TaskGroup $group) use ($closed) {
    var_dump($group->getResults(), count($group->getErrors()));
    $closed->complete(null);
});
$group->spawn(function () use ($group) {
    return 42;
});
unset($group);

(new Future($closed))->await();
?>
--EXPECT--
array(1) {
  [0]=>
  int(42)
}
int(0)
