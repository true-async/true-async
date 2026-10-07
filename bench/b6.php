<?php
// B6: one get_deadlocked_coroutines() call (dev/plans/S7.md, section 11).
// Usage: b6.php <pairs|graph|timers> [coroutines] [graph nodes]. Prints the call's wall time, the
// peak memory it added and the number of coroutines it found.
use function Async\spawn;
use function Async\await;
use function Async\suspend;
use function Async\delay;
use function Async\get_deadlocked_coroutines;

$mode = $argv[1] ?? 'pairs';
$count = (int) ($argv[2] ?? 10000);
$nodes = (int) ($argv[3] ?? 1000000);
$graph = [];

if ($mode === 'graph') {
    for ($i = 0; $i < $nodes; $i++) {
        $graph[] = new stdClass();
    }
}

function spawn_pair(array $graph): void
{
    $a = null;
    $b = null;
    $a = spawn(static function () use (&$b, $graph) {
        suspend();
        await($b);
    });
    $b = spawn(static function () use (&$a, $graph) {
        suspend();
        await($a);
    });
}

if ($mode === 'timers') {
    for ($i = 0; $i < $count; $i++) {
        spawn(static function () {
            delay(60000);
        });
    }
} else {
    for ($i = 0; $i < $count / 2; $i++) {
        spawn_pair($graph);
    }
}

// Two turns: every coroutine runs to its suspend(), then to its await.
suspend();
suspend();

memory_reset_peak_usage();
$before = memory_get_usage();
$start = hrtime(true);
$found = get_deadlocked_coroutines();
$elapsed = hrtime(true) - $start;
$peak = memory_get_peak_usage() - $before;

printf("%s: %d coroutines, %.1f ms, %.1f KiB peak, %d found\n",
    $mode, $count, $elapsed / 1e6, $peak / 1024, count($found));

foreach ($found as $coroutine) {
    $coroutine->cancel();
}

// Cancels the coroutines still on their timers.
exit(0);
