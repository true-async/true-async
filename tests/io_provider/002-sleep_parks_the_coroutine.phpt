--TEST--
sleep() and usleep() park their coroutine on a Timer op: the shorter sleep ends first, and sleep() returns 0
--FILE--
<?php
use function Async\spawn;

$started = hrtime(true);

spawn(function () {
    $left = sleep(1);
    echo "sleep(1) returned $left\n";
});
spawn(function () { usleep(100000); echo "usleep(100 ms) done\n"; });
spawn(function () { time_nanosleep(0, 200000000); echo "time_nanosleep(200 ms) done\n"; });

register_shutdown_function(function () use ($started) {
    echo "in parallel: ", var_export(hrtime(true) - $started < 1300 * 1000000, true), "\n";
});
?>
--EXPECT--
usleep(100 ms) done
time_nanosleep(200 ms) done
sleep(1) returned 0
in parallel: true
