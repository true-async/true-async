--TEST--
TaskGroup: map() on a temporary all() Future still completes when the group settles
--FILE--
<?php

use Async\Future;
use Async\FutureState;
use Async\TaskGroup;

$gate = new FutureState();
$group = new TaskGroup();
$group->spawn(fn() => (new Future($gate))->await());

$mapped = $group->all()->map(fn(array $results) => count($results) . " result");
$gate->complete("done");

var_dump($mapped->await());
?>
--EXPECT--
string(8) "1 result"
