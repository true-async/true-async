--TEST--
TaskGroup: await_all() refuses a group as its cancellation token, since a group is not Completable
--FILE--
<?php

use Async\TaskGroup;
use function Async\await_all;

try {
    await_all([], new TaskGroup());
} catch (TypeError $error) {
    echo $error->getMessage(), "\n";
}
?>
--EXPECT--
Async\await_all(): Argument #2 ($cancellation) must be of type ?Async\Completable, Async\TaskGroup given
