--TEST--
Pool: max_size enforced when factory yields during creation
--FILE--
<?php

use function Async\spawn;
use function Async\await;

/*
 * Bug: pool_create_resource() (factory) can yield (e.g. TCP connect).
 * active_count was incremented AFTER factory returned, so concurrent
 * coroutines all passed the `total < max_size` check and created
 * connections beyond the limit.
 *
 * Fix: increment active_count BEFORE factory, decrement on failure.
 */

$created = 0;
$started = 0;
$pool = new Async\Pool(
    function() use (&$created, &$started) {
        $id = ++$created;
        $started++;
        // A slow factory (TCP connect) that is still running when the others start: a timer could
        // end it before the next acquire() came in.
        while ($started < 3) Async\suspend();
        return "conn-$id";
    },
    null, null, null, null,
    0,  // min
    3,  // max
);

$coros = [];
for ($i = 0; $i < 10; $i++) {
    $coros[] = spawn(function() use ($pool, $i) {
        $conn = $pool->acquire();
        Async\suspend();  // hold connection
        $pool->release($conn);
    });
}

// Check mid-flight, once three factories have started
spawn(function() use ($pool, &$started) {
    while ($started < 3) {
        Async\suspend();
    }
    echo "During: pool=" . $pool->count() . "\n";
});

foreach ($coros as $c) await($c);

echo "Created: $created\n";
echo "After: pool=" . $pool->count() . "\n";
echo "Done\n";
?>
--EXPECT--
During: pool=3
Created: 3
After: pool=3
Done
