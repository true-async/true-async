--TEST--
TaskSet: the destructor rejects a pending joinNext() with the empty set's message
--FILE--
<?php

use Async\Future;
use Async\FutureState;
use Async\TaskSet;

$set = new TaskSet();
$set->spawn(fn() => (new Future(new FutureState()))->await());
$next = $set->joinNext();
unset($set);

try {
    $next->await();
} catch (Async\AsyncException $error) {
    echo get_class($error), ": ", $error->getMessage(), "\n";
}
?>
--EXPECT--
Async\AsyncException: Cannot race on an empty TaskGroup
