--TEST--
GC: the collector's run goes to the front of the queue, so 100 000 coroutines that fill the root buffer do not park until the fibers pass vm.max_map_count
--CONFLICTS--
fiber_stacks
--SKIPIF--
<?php
if (getenv('TRUE_ASYNC_SCHED') !== false) {
    die('skip the random pick of the fuzz lane takes the GC run off the front of the queue');
}
?>
--INI--
fiber.stack_size=1M
--FILE--
<?php

use function Async\spawn;
use function Async\suspend;

// A coroutine that adds a root to a full buffer must not park until the run ends: behind 100 000
// queued coroutines the parked fibers would pass vm.max_map_count ("Fiber stack protect failed").
// Main spawns all 100 000 before any runs, so the 10 000 that loop and the 10 000 that suspend
// once are parked together: 20 000 stacks, which Windows commits whole; at the default 2M they
// pass the CI runner's commit limit.
$count = 100000;
$survivors = [];

for ($i = 0; $i < $count; $i++) {
    $coroutine = spawn(function (int $kind) {
        if ($kind === 0) {
            for (;;) {
                suspend();
            }
        }

        if ($kind === 5) {
            suspend();
        }
    }, $i % 10);

    if ($i % 10 === 0) {
        $survivors[] = $coroutine;
    }
}

for ($i = 0; $i < 4; $i++) {
    suspend();
}

var_dump(gc_status()['runs'] > 0);

foreach ($survivors as $coroutine) {
    $coroutine->cancel();
}

suspend();
echo "done\n";

?>
--EXPECT--
bool(true)
done
