--TEST--
cpu_usage(): process_cores lies between the bounds from getrusage() and hrtime() read around the two calls
--SKIPIF--
<?php if (PHP_OS_FAMILY === 'Windows') die('skip getrusage() reads the process times on Unix only'); ?>
--FILE--
<?php

function process_ns(): int
{
    $usage = getrusage();

    return ($usage['ru_utime.tv_sec'] + $usage['ru_stime.tv_sec']) * 1_000_000_000
        + ($usage['ru_utime.tv_usec'] + $usage['ru_stime.tv_usec']) * 1000;
}

$wall_before_first = hrtime(true);
$process_before_first = process_ns();
Async\cpu_usage();
$process_after_first = process_ns();
$wall_after_first = hrtime(true);

// Runs until the process has spent 10 ms of CPU, so the lower bound is above zero.
while (process_ns() - $process_after_first < 10_000_000) {
}

$wall_before_second = hrtime(true);
$process_before_second = process_ns();
$usage = Async\cpu_usage();
$process_after_second = process_ns();
$wall_after_second = hrtime(true);

$lower = ($process_before_second - $process_after_first) / ($wall_after_second - $wall_before_first);
$upper = ($process_after_second - $process_before_first) / ($wall_before_second - $wall_after_first);

var_dump($lower > 0);
var_dump($lower * (1 - 1e-9) <= $usage['process_cores'] && $usage['process_cores'] <= $upper * (1 + 1e-9));
?>
--EXPECT--
bool(true)
bool(true)
