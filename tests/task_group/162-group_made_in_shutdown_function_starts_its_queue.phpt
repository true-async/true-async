--TEST--
TaskGroup: a group made in a shutdown function after exit() starts its queued tasks
--FILE--
<?php

use Async\TaskGroup;

register_shutdown_function(function () {
    $group = new TaskGroup(concurrency: 1);
    $group->spawn(fn() => 1);
    $group->spawn(fn() => 2);
    var_dump($group->all()->await());
});

exit(0);
?>
--EXPECT--
array(2) {
  [0]=>
  int(1)
  [1]=>
  int(2)
}
