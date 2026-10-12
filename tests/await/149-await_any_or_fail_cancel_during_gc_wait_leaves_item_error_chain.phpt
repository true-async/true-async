--TEST--
await_any_or_fail(): with a GC run due where a later item's error is chained over an earlier one, a cancel of the waiter is not chained into the items' stored errors
--FILE--
<?php

use Async\Future;

// The second item's error already has the first as its previous, so chaining them releases the first. Collected
// before and one root short of the core's wait for a queued collection (100 000 roots past the threshold) after,
// that release starts the collection, where a wait would let the queued coroutine cancel the waiter and the throw
// that follows chain the cancellation into the first error.
$first = new Exception("first");
$second = new Exception("second", 0, $first);
$failed_first = Future::failed($first);
$failed_second = Future::failed($second);
// Under another order (a random scheduling seed) the cancel comes before the waiter reads them.
$failed_first->ignore();
$failed_second->ignore();
gc_collect_cycles();
$held = [];
$runs = gc_status()['runs'];

$waiter = Async\spawn(function () use ($failed_first, $failed_second, &$held) {
    $status = gc_status();

    for ($i = $status['threshold'] - $status['roots'] - 1 + 100000; $i > 0; $i--) {
        $object = new stdClass();
        $held[] = $object;
        unset($object);
    }

    Async\await_any_or_fail([$failed_first, $failed_second]);
});

Async\spawn(function () use (&$waiter) {
    $waiter->cancel();
});

// The waiter ends with the cancellation or with the item's error, as later waits fall; the stored chain is checked.
try {
    Async\await($waiter);
} catch (Throwable) {
}

echo "collection after: ", gc_status()['runs'] > $runs ? "yes" : "no", "\n";
echo "first's previous: ", $first->getPrevious() === null ? "none" : get_class($first->getPrevious()), "\n";
?>
--EXPECT--
collection after: yes
first's previous: none
