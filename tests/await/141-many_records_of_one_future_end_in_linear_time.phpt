--TEST--
Waits with many records on one Future unlink each record at its slot, with no search: a thousand copies of an item leave the Future whether another item or the repeated one completes, and for two waits sharing the copies
--DESCRIPTION--
A search per record made the unlink quadratic in the copies (DECISIONS 2026-10-07); the count of searches stands for the time.
--FILE--
<?php
use Async\Future;
use Async\FutureState;
use TrueAsync\Test;
use function Async\spawn;
use function Async\await;
use function Async\await_any_or_fail;
use function Async\suspend;

$pending = new FutureState();
$future = new Future($pending);
$done = new FutureState();
$searches = Test\callback_searches();
$waiter = spawn(fn() => await_any_or_fail(array_merge(array_fill(0, 1000, $future), [new Future($done)])));
suspend();
$done->complete("done");
var_dump(await($waiter));

$waiter = spawn(fn() => await_any_or_fail(array_fill(0, 1000, $future)));
suspend();
$pending->complete("repeated");
var_dump(await($waiter));

$pending = new FutureState();
$items = array_fill(0, 1000, new Future($pending));
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
echo "searches: ", Test\callback_searches() - $searches, "\n";
?>
--EXPECT--
string(4) "done"
string(8) "repeated"
string(5) "first"
string(6) "second"
searches: 0
