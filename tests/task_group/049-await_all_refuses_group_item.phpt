--TEST--
TaskGroup: await_all() refuses a group as an item, since a group is not Completable
--FILE--
<?php

use Async\TaskGroup;
use function Async\await_all;

try {
    await_all([new TaskGroup()]);
} catch (Async\AsyncException $error) {
    echo $error->getMessage(), "\n";
}
?>
--EXPECT--
Expected item to be an Async\Completable object
