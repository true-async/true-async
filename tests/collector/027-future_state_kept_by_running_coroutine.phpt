--TEST--
get_deadlocked_coroutines(): a coroutine awaiting a Future whose FutureState a coroutine waiting on a timer keeps is not reported
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
    $state = new FutureState();
    $future = new Future($state);
    spawn(function () use ($future) {
        $value = $future->await();
        echo "awaited: ", $value, "\n";
        $GLOBALS['awaited'] = true;
    });
    spawn(function () use ($state) {
        // Parked on a timer until main has checked: one delay could end before the check
        while (!$GLOBALS['checked']) {
            delay(1);
        }

        $state->complete("value");
    });
}

$checked = false;
$awaited = false;
start();

for ($i = 0; $i < 4; $i++) {
    suspend();
}

echo count(get_deadlocked_coroutines()), " found\n";
$checked = true;

while (!$awaited) {
    delay(1);
}

echo "end\n";
?>
--EXPECT--
0 found
awaited: value
end
