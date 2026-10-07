--TEST--
A FutureState scanned by the collector while a waiter holds its event reports nothing for it: the waiter still gets the result
--FILE--
<?php
use Async\Future;
use Async\FutureState;
use function Async\spawn;
use function Async\await;
use function Async\suspend;

$state = new FutureState();
$future = new Future($state);
$waiter = spawn(fn() => await($future));
suspend();

$cycle = new stdClass();
$cycle->self = $cycle;
$cycle->state = $state;
unset($cycle);

var_dump(gc_collect_cycles());
$state->complete(1);
var_dump(await($waiter));
?>
--EXPECT--
int(1)
int(1)
