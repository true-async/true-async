--TEST--
Waits with many records on one Future end in linear time: a million copies of an item unlink well under the time limit, whether another item or the repeated one completes, and for two waits sharing the copies
--INI--
max_execution_time=20
memory_limit=1G
--FILE--
<?php
use Async\Future;
use Async\FutureState;
use function Async\spawn;
use function Async\await;
use function Async\await_any_or_fail;
use function Async\suspend;

$pending = new FutureState();
$future = new Future($pending);
$done = new FutureState();

$waiter = spawn(fn() => await_any_or_fail(array_merge(array_fill(0, 1000000, $future), [new Future($done)])));
suspend();
$done->complete("done");
var_dump(await($waiter));

$waiter = spawn(fn() => await_any_or_fail(array_fill(0, 1000000, $future)));
suspend();
$pending->complete("repeated");
var_dump(await($waiter));

$pending = new FutureState();
$items = array_fill(0, 1000000, new Future($pending));
$first = new FutureState();
$second = new FutureState();
$a = spawn(fn() => await_any_or_fail([...$items, new Future($first)]));
$b = spawn(fn() => await_any_or_fail([...$items, new Future($second)]));
suspend();
$first->complete("first");
var_dump(await($a));
$second->complete("second");
var_dump(await($b));
$pending->complete(0);
?>
--EXPECT--
string(4) "done"
string(8) "repeated"
string(5) "first"
string(6) "second"
