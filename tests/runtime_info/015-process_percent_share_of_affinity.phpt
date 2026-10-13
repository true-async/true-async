--TEST--
cpu_usage(): process_percent is process_cores as a share of the CPUs the process may use, here one
--SKIPIF--
<?php if (PHP_OS_FAMILY !== 'Linux') die('skip the affinity count is replaced on Linux only'); ?>
--FILE--
<?php

// A root without cgroup files: no quota, whatever the CI machine's.
TrueAsync\Test\cpu_limits('/nonexistent-runtime-info-root', 1);
Async\cpu_usage();

$sum = 0;
for ($i = 0; $i < 2_000_000; $i++) {
    $sum += $i;
}

$usage = Async\cpu_usage();
var_dump(abs($usage['process_percent'] - min($usage['process_cores'] * 100, 100)) < 1e-9);
?>
--EXPECT--
bool(true)
