--TEST--
TaskSet: joinAll() takes the set's entries before it builds the composite, so a task spawned during a GC wait inside it stays in the set
--FILE--
<?php

use Async\CompositeException;
use Async\Future;
use Async\FutureState;
use Async\TaskSet;

// One root short of the threshold puts the release that starts the collection inside joinAll() on the pinned
// core: the spawner runs during that wait and adds a running and a queued task, which joinAll() must leave in the
// set. The last line shows the collection ran there, or the test exercises nothing.
$set = new TaskSet(concurrency: 1);
$set->spawn(fn() => throw new Exception("error"));

while (!$set->isFinished()) {
    Async\suspend();
}

$spawned = false;

Async\spawn(function () use ($set, &$spawned) {
    $set->spawn(fn() => (new Future(new FutureState()))->await());
    $set->spawn(fn() => "queued");
    $spawned = true;
});

$held = [];
$status = gc_status();

for ($i = $status['threshold'] - $status['roots'] - 1; $i > 0; $i--) {
    $object = new stdClass();
    $held[] = $object;
    unset($object);
}

$runs = gc_status()['runs'];
$future = $set->joinAll();
$collected_inside = $spawned && gc_status()['runs'] > $runs;

try {
    $future->await();
} catch (CompositeException $composite) {
    echo count($composite->getExceptions()), " error\n";
}

echo "left in the set: ", count($set), "\n";
echo "collection inside joinAll(): ", $collected_inside ? "yes" : "no", "\n";
$set->cancel();
?>
--EXPECT--
1 error
left in the set: 2
collection inside joinAll(): yes
