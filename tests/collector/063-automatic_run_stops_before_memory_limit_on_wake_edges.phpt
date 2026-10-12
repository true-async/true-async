--TEST--
The automatic run: coroutines awaiting the same dead Futures make far more wake edges than nodes, and a walk whose edges would take the memory in use past memory_limit stops and finds nothing, without a fatal error
--DESCRIPTION--
The margin is measured on this build's heap: the smallest one at which a walk finds one coroutine awaiting
5000 Futures has room for the nodes, which fifty awaiting 5000 Futures nearly match, and not for their edges.
--INI--
true_async.partial_deadlock_interval=0
--FILE--
<?php
use Async\Future;
use Async\FutureState;
use TrueAsync\Test;
use function Async\spawn;
use function Async\await;
use function Async\await_all;
use function Async\suspend;
use function Async\delay;
use function Async\get_deadlocked_coroutines;

$parked = 0;
$warnings = 0;
set_error_handler(function () use (&$warnings) {
    $warnings++;
});

/* $count coroutines awaiting the same 5000 Futures, which only they hold, so a walk finds them dead. */
function park(int $count): void
{
    global $parked;

    $dead = [];

    for ($i = 0; $i < 5000; $i++) {
        $dead[] = new Future(new FutureState());
    }

    $parked = 0;

    for ($c = 0; $c < $count; $c++) {
        spawn(function () use ($dead) {
            $GLOBALS['parked']++;
            await_all($dead);
        });
    }

    // Each wait takes its records before a limit is set.
    while ($parked < $count) {
        suspend();
    }
}

/* One automatic run with memory_limit $margin MiB above the memory in use; the coroutines it reported. */
function walk_at(int $margin): int
{
    global $warnings;

    $before = $warnings;
    // Cached chunks count as memory in use and would give the walk room the limit does not show.
    gc_mem_caches();
    ini_set('memory_limit', (string) (memory_get_usage(true) + $margin * 1048576));
    $runs = Test\collector_idle_runs();

    // A delay that ends before the scheduler goes idle makes no run.
    while (Test\collector_idle_runs() === $runs) {
        delay(1);
    }

    ini_set('memory_limit', '-1');

    return $warnings - $before;
}

park(1);
$nodes_fit = 1;

while ($nodes_fit <= 8 && walk_at($nodes_fit) === 0) {
    $nodes_fit++;
}

echo "one coroutine found at a margin above a megabyte: ", var_export($nodes_fit > 1 && $nodes_fit <= 8, true), "\n";

foreach (get_deadlocked_coroutines() as $coroutine) {
    $coroutine->cancel();

    try {
        await($coroutine);
    } catch (Async\AsyncCancellation) {
    }
}

park(50);
echo "fifty at that margin: ", walk_at($nodes_fit), " reported\n";

$found = 0;

for ($margin = $nodes_fit + 1; $margin <= 16 && $found === 0; $margin++) {
    $found = walk_at($margin);
}

echo "fifty at a larger margin: ", $found, " reported\n";

foreach (get_deadlocked_coroutines() as $coroutine) {
    $coroutine->cancel();
}
?>
--EXPECT--
one coroutine found at a margin above a megabyte: true
fifty at that margin: 0 reported
fifty at a larger margin: 50 reported
