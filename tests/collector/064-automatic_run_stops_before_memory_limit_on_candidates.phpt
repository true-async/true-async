--TEST--
The automatic run: a walk whose candidates alone would take the memory in use past memory_limit, when the node table and its index double for them, stops and finds nothing, without a fatal error
--SKIPIF--
<?php
if (getenv('SKIP_ASAN') || getenv('SKIP_SLOW_TESTS')) die('skip 16 500 parked coroutines cost minutes of system time under ASAN');
if (PHP_OS_FAMILY === 'Windows') die('skip Windows commits each fiber stack in full: 16 500 stacks of 2 MB');
?>
--INI--
true_async.partial_deadlock_interval=0
--FILE--
<?php
use Async\Future;
use Async\FutureState;
use TrueAsync\Test;
use function Async\spawn;
use function Async\suspend;
use function Async\delay;
use function Async\get_deadlocked_coroutines;

function start(): void
{
    for ($c = 0; $c < 16500; $c++) {
        spawn(function () {
            $GLOBALS['parked']++;
            (new Future(new FutureState()))->await();
        });
    }
}

$warnings = 0;
set_error_handler(function () use (&$warnings) {
    $warnings++;
});

$parked = 0;
start();

// Every coroutine runs before the limits below.
while ($parked < 16500) {
    suspend();
}

// A megabyte is below the heap chunk the ceiling keeps spare, so it stops the run at its first
// candidate. The tables double past 16 384 candidates to a few megabytes: the larger limits leave room
// for the walk until then and none for that doubling. A walk stops and finds nothing, or finds every
// coroutine.
for ($margin = 1; $margin <= 8; $margin++) {
    $before = $warnings;
    ini_set('memory_limit', (string) (memory_get_usage(true) + $margin * 1048576));
    $runs = Test\collector_idle_runs();

    // A delay that ends before the scheduler goes idle makes no run.
    while (Test\collector_idle_runs() === $runs) {
        delay(1);
    }

    echo "$margin MiB above the memory in use: ", $warnings === $before ? "stopped" : "found " . ($warnings - $before), "\n";
}

ini_set('memory_limit', '-1');
$runs = Test\collector_idle_runs();

while (Test\collector_idle_runs() === $runs) {
    delay(1);
}

echo "without a limit: ", $warnings, " warnings\n";

foreach (get_deadlocked_coroutines() as $coroutine) {
    $coroutine->cancel();
}
?>
--EXPECT--
1 MiB above the memory in use: stopped
2 MiB above the memory in use: stopped
3 MiB above the memory in use: stopped
4 MiB above the memory in use: stopped
5 MiB above the memory in use: stopped
6 MiB above the memory in use: stopped
7 MiB above the memory in use: stopped
8 MiB above the memory in use: stopped
without a limit: 16500 warnings
