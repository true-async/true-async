--TEST--
TaskGroup: a dropped map() chain on a pending read survives a collection and runs when the task ends
--FILE--
<?php

use Async\Future;
use Async\FutureState;
use Async\TaskGroup;

$gate = new FutureState();
$group = new TaskGroup();
$group->spawn(fn() => (new Future($gate))->await());

$mapped = new FutureState();
$group->race()->map(function ($value) use ($mapped) {
    echo "mapped: ", $value, "\n";
    $mapped->complete(null);
})->ignore();

var_dump(gc_collect_cycles());
$gate->complete("done");
(new Future($mapped))->await();
$group->close();
?>
--EXPECT--
int(0)
mapped: done
