--TEST--
All sleep functions async comparison: sleep(), usleep(), time_nanosleep(), time_sleep_until()
--SKIPIF--
<?php
if (!function_exists("sleep") || !function_exists("usleep") ||
    !function_exists("time_nanosleep") || !function_exists("time_sleep_until")) {
    echo "skip one or more sleep functions are not available";
}
?>
--FILE--
<?php

use function Async\await_all_or_fail;
use function Async\spawn;

echo "Starting full sleep functions test\n";

// The order the sleeps end in and an upper bound are the clock's: a process
// stopped at a park wakes a short sleep before the next one has started. The
// results print once all have ended, each checked against the time it asked for.
$sleep = spawn(function () {
    echo "Testing sleep() async\n";
    $start = microtime(true);
    $result = sleep(1);
    $elapsed = microtime(true) - $start;
    return "sleep(1) result: $result, elapsed >= 1s: " . ($elapsed >= 1 ? "yes" : "no, $elapsed");
});

$usleep = spawn(function () {
    echo "Testing usleep() async\n";
    $start = microtime(true);
    usleep(200000); // 0.2s
    $elapsed = microtime(true) - $start;
    return "usleep(200000) elapsed >= 0.2s: " . ($elapsed >= 0.2 ? "yes" : "no, $elapsed");
});

$nanosleep = spawn(function () {
    echo "Testing time_nanosleep() async\n";
    $start = microtime(true);
    $result = time_nanosleep(0, 100000000); // 0.1s
    $elapsed = microtime(true) - $start;
    return "time_nanosleep(0, 100000000) result: " . ($result ? "true" : "false")
        . ", elapsed >= 0.1s: " . ($elapsed >= 0.1 ? "yes" : "no, $elapsed");
});

$sleep_until = spawn(function () {
    echo "Testing time_sleep_until() async\n";
    $target = microtime(true) + 0.15;
    $result = time_sleep_until($target);
    $now = microtime(true);
    return "time_sleep_until() result: " . ($result ? "true" : "false")
        . ", target reached: " . ($now >= $target ? "yes" : "no, " . ($target - $now) . "s short");
});

spawn(function() {
    echo "Background task running\n";
});

spawn(function () use ($sleep, $usleep, $nanosleep, $sleep_until) {
    foreach (await_all_or_fail([$nanosleep, $sleep_until, $usleep, $sleep]) as $line) {
        echo $line, "\n";
    }
});

echo "Full sleep functions test completed\n";
?>
--EXPECT--
Starting full sleep functions test
Full sleep functions test completed
Testing sleep() async
Testing usleep() async
Testing time_nanosleep() async
Testing time_sleep_until() async
Background task running
time_nanosleep(0, 100000000) result: true, elapsed >= 0.1s: yes
time_sleep_until() result: true, target reached: yes
usleep(200000) elapsed >= 0.2s: yes
sleep(1) result: 0, elapsed >= 1s: yes
