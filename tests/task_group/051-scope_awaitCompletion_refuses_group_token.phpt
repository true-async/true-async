--TEST--
TaskGroup: Scope::awaitCompletion() refuses a group as its cancellation token, since a group is not Completable
--FILE--
<?php

use Async\Scope;
use Async\TaskGroup;

try {
    (new Scope())->awaitCompletion(new TaskGroup());
} catch (TypeError $error) {
    echo $error->getMessage(), "\n";
}
?>
--EXPECT--
Async\Scope::awaitCompletion(): Argument #1 ($cancellation) must be of type Async\Completable, Async\TaskGroup given
