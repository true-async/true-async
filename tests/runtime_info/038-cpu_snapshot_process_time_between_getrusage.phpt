--TEST--
CpuSnapshot::now(): processUserNs and processSystemNs lie between getrusage() read just before and just after
--SKIPIF--
<?php if (PHP_OS_FAMILY === 'Windows') die('skip getrusage() reads the process times on Unix only'); ?>
--FILE--
<?php

function process_ns(string $kind): int
{
    $usage = getrusage();

    return [
        'user' => $usage['ru_utime.tv_sec'] * 1_000_000_000 + $usage['ru_utime.tv_usec'] * 1000,
        'system' => $usage['ru_stime.tv_sec'] * 1_000_000_000 + $usage['ru_stime.tv_usec'] * 1000,
    ][$kind];
}

// Some user time first, so the microsecond part is not zero.
$start = process_ns('user');
while (process_ns('user') - $start < 5_000_000) {
}

$user_before = process_ns('user');
$system_before = process_ns('system');
$snapshot = Async\CpuSnapshot::now();
$user_after = process_ns('user');
$system_after = process_ns('system');

var_dump($snapshot->processUserNs > 0);
var_dump($user_before <= $snapshot->processUserNs && $snapshot->processUserNs <= $user_after);
var_dump($system_before <= $snapshot->processSystemNs && $snapshot->processSystemNs <= $system_after);
?>
--EXPECT--
bool(true)
bool(true)
bool(true)
