--TEST--
TaskSet: the destructor rejects a pending joinNext() made after a pending joinAll() at once
--FILE--
<?php

use Async\Future;
use Async\FutureState;
use Async\TaskSet;

$set = new TaskSet();
$set->spawn(fn() => (new Future(new FutureState()))->await());
$all = $set->joinAll();
$next = $set->joinNext();
unset($set);

echo "joinAll completed: ", var_export($all->isCompleted(), true), "\n";
echo "joinNext completed: ", var_export($next->isCompleted(), true), "\n";

try {
    $next->await();
} catch (Async\AsyncException $e) {
    echo $e->getMessage(), "\n";
}

$all->ignore();
?>
--EXPECT--
joinAll completed: false
joinNext completed: true
Cannot race on an empty TaskGroup
