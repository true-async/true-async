--TEST--
TaskGroup: exit() in a task leaves a null result, as Coroutine::getResult() gives
--FILE--
<?php

$group = new Async\TaskGroup();

register_shutdown_function(function () use ($group) {
    var_dump($group->getResults());
});

$group->spawn(fn() => exit(0));
?>
--EXPECT--
array(1) {
  [0]=>
  NULL
}
