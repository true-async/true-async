--TEST--
The automatic run: coroutines awaiting the same dead Futures make far more wake edges than nodes, and a walk whose edges would take the memory in use past memory_limit stops and finds nothing, without a fatal error
--INI--
true_async.partial_deadlock_interval=0
--FILE--
<?php
use Async\Future;
use Async\FutureState;
use function Async\spawn;
use function Async\await_all;
use function Async\suspend;
use function Async\delay;
use function Async\get_deadlocked_coroutines;

function start(): void
{
    $dead = [];

    for ($i = 0; $i < 5000; $i++) {
        $dead[] = new Future(new FutureState());
    }

    for ($c = 0; $c < 50; $c++) {
        spawn(function () use ($dead) {
            await_all($dead);
        });
    }
}

$warnings = 0;
set_error_handler(function () use (&$warnings) {
    $warnings++;
});

start();
suspend();

// The edge table doubles to a few megabytes: some of these limits leave room for the nodes and
// none for the next doubling of the edges.
for ($margin = 1; $margin <= 8; $margin++) {
    ini_set('memory_limit', (string) (memory_get_usage(true) + $margin * 1048576));
    delay(1);
}

ini_set('memory_limit', '-1');
delay(1);
echo "without a limit: ", $warnings, " warnings\n";

foreach (get_deadlocked_coroutines() as $coroutine) {
    $coroutine->cancel();
}
?>
--EXPECT--
without a limit: 50 warnings
