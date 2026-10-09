--TEST--
TaskGroup: the free of its idle parent Scope object closes the group's own scope, and spawn() then throws
--FILE--
<?php

use Async\Scope;
use Async\TaskGroup;

$parent = new Scope();
$group = Async\await($parent->spawn(fn() => new TaskGroup()));

unset($parent);

try {
    $group->spawn(function () {
        echo "task ran\n";
    });
} catch (Async\AsyncException $error) {
    echo $error->getMessage(), "\n";
}

var_dump($group->isClosed());
?>
--EXPECT--
Cannot spawn a coroutine in a closed scope
bool(true)
