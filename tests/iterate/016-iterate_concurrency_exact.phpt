--TEST--
iterate() - concurrency limit is exact when callbacks suspend
--FILE--
<?php

use function Async\spawn;
use function Async\suspend;
use function Async\iterate;

echo "start\n";

// suspend() reschedules the coroutine immediately, so the iteration loop drops back out of the
// callback before the freshly spawned workers ever enter it and the real peak stays invisible.
// Each callback suspends a fixed number of times, so every worker spawned is inside at once, too
// few or too many alike; a timer could end one callback before the next worker has entered.
spawn(function () {
    foreach ([1, 2, 3] as $concurrency) {
        $active = 0;
        $peak = 0;

        iterate(range(1, 8), function () use (&$active, &$peak) {
            $active++;

            if ($active > $peak) {
                $peak = $active;
            }

            for ($i = 0; $i < 20; $i++) {
                suspend();
            }

            $active--;
        }, concurrency: $concurrency);

        echo "concurrency $concurrency: peak $peak\n";
    }

    echo "done\n";
});

echo "end\n";
?>
--EXPECT--
start
end
concurrency 1: peak 1
concurrency 2: peak 2
concurrency 3: peak 3
done
