--TEST--
GC: gc_collect_cycles() returns the count of the run it waited for, though another run finished before it resumed
--FILE--
<?php

use function Async\await;
use function Async\spawn;

// The first run wakes its waiter at the tail, behind the second caller, whose own run goes to the front of the
// queue, finds nothing and finishes before the first caller resumes.
$first = spawn(function () {
    $cycle = new stdClass();
    $cycle->self = $cycle;
    unset($cycle);
    $count = gc_collect_cycles();
    echo "first: $count\n";
});

$second = spawn(function () {
    $count = gc_collect_cycles();
    echo "second: $count\n";
});

await($first);
await($second);

?>
--EXPECT--
first: 1
second: 0
