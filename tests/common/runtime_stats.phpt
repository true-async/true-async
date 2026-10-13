--TEST--
Async\runtime_stats(): safe to call at the top level before any spawn (#164)
--FILE--
<?php

use function Async\runtime_stats;
use function Async\spawn;
use function Async\await;

// Called at the top level before any spawn/await. This used to abort the
// process (assert head < capacity, i.e. 0 < 0) on a debug build; it must now
// return the full set of keys.
$stats = runtime_stats();
var_dump(is_array($stats));

$keys = ['coroutines_total', 'coroutines_active', 'microtasks_queue',
         'coroutine_queue', 'resumed_queue', 'fiber_pool_count',
         'fiber_pool_capacity', 'fiber_pool_min', 'fiber_stack_size',
         'fiber_pool_virtual_bytes'];
var_dump(array_diff($keys, array_keys($stats)) === []);

// The RFC core starts the scheduler with the script, so only the main coroutine is counted;
// static fields still present.
var_dump($stats['coroutines_total'] === 1);
var_dump($stats['fiber_pool_virtual_bytes'] === 0);
var_dump(is_int($stats['fiber_pool_min']));

// Inside a coroutine the scheduler is running, so live values are reported.
await(spawn(function (): void {
    var_dump(runtime_stats()['coroutines_total'] >= 1);
}));

echo "done\n";
?>
--EXPECT--
bool(true)
bool(true)
bool(true)
bool(true)
bool(true)
bool(true)
done
