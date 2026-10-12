--TEST--
TaskGroup: all() builds its composite after the walk, so a spawn during a GC wait inside it grows no table under the walk
--FILE--
<?php

use Async\TaskGroup;

// Eight tasks fill the table's first allocation; the ninth, spawned while all() waits for the collection that its
// release starts, grows it. One root short of the threshold puts that release inside all() on the pinned core;
// the last line shows the collection ran there, or the test exercises nothing.
$group = new TaskGroup();

for ($i = 0; $i < 8; $i++) {
    $group->spawn(function () use ($i) {
        if ($i < 2) {
            throw new Exception("error $i");
        }

        return $i;
    });
}

while (!$group->isFinished()) {
    Async\suspend();
}

$spawned = false;

Async\spawn(function () use ($group, &$spawned) {
    $group->spawn(fn() => 8);
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
$future = $group->all();
$collected_inside = $spawned && gc_status()['runs'] > $runs;

try {
    $future->await();
} catch (Async\CompositeException $composite) {
    echo implode(", ", array_map(fn($error) => $error->getMessage(), $composite->getExceptions())), "\n";
}

echo "collection inside all(): ", $collected_inside ? "yes" : "no", "\n";
?>
--EXPECT--
error 0, error 1
collection inside all(): yes
