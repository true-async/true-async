--TEST--
sleep() and usleep() park their coroutine on a Timer op: the shorter sleep ends first, and sleep() returns 0
--FILE--
<?php
use function Async\spawn;
use function Async\await;

$main_resumed = false;
$deadlines = [];
$ended = [];
$parked_all = true;

function sleeper(string $name, int $nanoseconds, callable $sleep): Async\Coroutine
{
    return spawn(function () use ($name, $nanoseconds, $sleep) {
        global $main_resumed, $deadlines, $ended, $parked_all;

        $deadlines[$name] = hrtime(true) + $nanoseconds;
        $result = $sleep();
        // A sleep that blocked the thread ends before main runs again.
        $parked_all = $parked_all && $main_resumed;
        $ended[] = $name;

        return $result;
    });
}

// Armed out of the order of their deadlines; the order expected is read from the deadlines they took.
$sleepers = [
    sleeper("sleep(1)", 1000000000, fn() => sleep(1)),
    sleeper("usleep(100 ms)", 100000000, fn() => usleep(100000)),
    sleeper("time_nanosleep(200 ms)", 200000000, fn() => time_nanosleep(0, 200000000)),
];

Async\suspend();
$main_resumed = true;
$left = await($sleepers[0]);

foreach ($sleepers as $sleeper) {
    await($sleeper);
}

asort($deadlines);
echo "parked: ", var_export($parked_all, true), "\n";
echo "ended in the order of their deadlines: ", var_export(array_keys($deadlines) === $ended, true), "\n";
echo "sleep(1) returned $left\n";
?>
--EXPECT--
parked: true
ended in the order of their deadlines: true
sleep(1) returned 0
