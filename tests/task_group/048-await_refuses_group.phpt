--TEST--
TaskGroup: await() refuses a group, since a group is not Completable
--FILE--
<?php

use Async\TaskGroup;
use function Async\await;

try {
    await(new TaskGroup());
} catch (TypeError $error) {
    echo $error->getMessage(), "\n";
}
?>
--EXPECT--
Async\await(): Argument #1 ($awaitable) must be of type Async\Completable, Async\TaskGroup given
