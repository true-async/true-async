--TEST--
available_parallelism(): without a quota, the count of CPUs in the process's affinity mask
--SKIPIF--
<?php
if (PHP_OS_FAMILY !== 'Linux') die('skip the affinity mask is read from /proc on Linux only');
if (!preg_match('/^Cpus_allowed_list:\s*(\S+)$/m', (string) @file_get_contents('/proc/self/status'))) die('skip no Cpus_allowed_list');
?>
--FILE--
<?php

// "0-3,6" in /proc/self/status: the same mask sched_getaffinity() reads.
preg_match('/^Cpus_allowed_list:\s*(\S+)$/m', file_get_contents('/proc/self/status'), $match);
$allowed = 0;
foreach (explode(',', $match[1]) as $range) {
    $bounds = explode('-', $range);
    $allowed += count($bounds) === 2 ? (int) $bounds[1] - (int) $bounds[0] + 1 : 1;
}

// A root without cgroup files: no quota, whatever the CI machine's; the affinity count is not replaced.
TrueAsync\Test\cpu_limits('/nonexistent-runtime-info-root');
var_dump(Async\available_parallelism() === $allowed);
?>
--EXPECT--
bool(true)
