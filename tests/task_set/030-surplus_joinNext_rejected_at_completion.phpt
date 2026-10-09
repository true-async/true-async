--TEST--
TaskSet: a joinNext() with nothing left to take rejects when the set completes
--FILE--
<?php

use Async\TaskSet;

$set = new TaskSet();
$set->spawn(fn() => "only");

$first = $set->joinNext();
$surplus = $set->joinNext();
var_dump($first->await());

$set->close();

try {
    $surplus->await();
} catch (Async\AsyncException $error) {
    echo $error->getMessage(), "\n";
}
?>
--EXPECT--
string(4) "only"
Cannot race on an empty TaskGroup
