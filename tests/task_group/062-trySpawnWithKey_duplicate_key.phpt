--TEST--
TaskGroup: trySpawnWithKey() with a key already present throws, as spawnWithKey() does
--FILE--
<?php

use Async\TaskGroup;

$group = new TaskGroup();
$group->spawnWithKey("job", fn() => 1);

try {
    $group->trySpawnWithKey("job", fn() => 2);
} catch (Async\AsyncException $error) {
    echo $error->getMessage(), "\n";
}

var_dump($group->all()->await());
?>
--EXPECT--
Duplicate key "job" in TaskGroup
array(1) {
  ["job"]=>
  int(1)
}
