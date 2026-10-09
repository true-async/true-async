--TEST--
TaskSet: two pending joinNext() calls take different tasks
--FILE--
<?php

use Async\TaskSet;

$set = new TaskSet();
$set->spawn(fn() => "first");
$set->spawn(fn() => "second");

$first = $set->joinNext();
$second = $set->joinNext();
var_dump($first->await(), $second->await());
?>
--EXPECT--
string(5) "first"
string(6) "second"
