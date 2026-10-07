--TEST--
The automatic run: a walk whose candidates alone would take the memory in use past memory_limit, when the node table and its index double for them, stops and finds nothing, without a fatal error
--SKIPIF--
<?php
if (getenv('SKIP_ASAN') || getenv('SKIP_SLOW_TESTS')) die('skip 16 500 parked coroutines cost minutes of system time under ASAN');
?>
--INI--
true_async.partial_deadlock_interval=0
--FILE--
<?php
use Async\Future;
use Async\FutureState;
use function Async\spawn;
use function Async\suspend;
use function Async\delay;
use function Async\get_deadlocked_coroutines;

function start(): void
{
    for ($c = 0; $c < 16500; $c++) {
        spawn(function () {
            (new Future(new FutureState()))->await();
        });
    }
}

$warnings = 0;
set_error_handler(function () use (&$warnings) {
    $warnings++;
});

start();
suspend();

// A megabyte is below the heap chunk the ceiling keeps spare, so it stops the run at its first
// candidate. The tables double past 16 384 candidates to a few megabytes: some of the larger limits
// leave room for the walk until then and none for that doubling.
for ($margin = 1; $margin <= 8; $margin++) {
    ini_set('memory_limit', (string) (memory_get_usage(true) + $margin * 1048576));
    delay(1);

    if ($margin == 1) {
        echo "a megabyte above the memory in use: ", $warnings, " warnings\n";
    }
}

ini_set('memory_limit', '-1');
delay(1);
echo "without a limit: ", $warnings, " warnings\n";

foreach (get_deadlocked_coroutines() as $coroutine) {
    $coroutine->cancel();
}
?>
--EXPECT--
a megabyte above the memory in use: 0 warnings
without a limit: 16500 warnings
