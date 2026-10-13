--TEST--
get_deadlocked_coroutines(): a coroutine waiting on a timer and the one awaiting it are not reported
--FILE--
<?php
use function Async\spawn;
use function Async\await;
use function Async\suspend;
use function Async\get_deadlocked_coroutines;
use function Async\delay;

function start(int &$parked, bool &$looked): WeakReference
{
    $sleeper = spawn(function () use (&$parked, &$looked) {
        $parked++;

        // On its timer until main has looked: a timer that fired first would leave no timer to look at.
        do {
            delay(50);
        } while (!$looked);

        echo "timer fired\n";
    });
    spawn(function () use ($sleeper, &$parked) {
        $parked++;
        await($sleeper);
        echo "waiter woke\n";
    });

    // Weak: main holds neither coroutine when it asks the collector.
    return WeakReference::create($sleeper);
}

$parked = 0;
$looked = false;
$sleeper = start($parked, $looked);

// Waits until both have parked and the sleeper is back on its timer.
while ($parked < 2 || $sleeper->get()->isQueued()) {
    suspend();
}

echo count(get_deadlocked_coroutines()), " found\n";
$looked = true;
?>
--EXPECT--
0 found
timer fired
waiter woke
