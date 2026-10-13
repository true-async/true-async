--TEST--
cpu_usage(): under a quota of half a CPU, process_percent is process_cores as a share of 0.5, at most 100
--SKIPIF--
<?php if (PHP_OS_FAMILY !== 'Linux') die('skip the cgroup files are read on Linux only'); ?>
--FILE--
<?php

require __DIR__ . '/inc/cgroup_fixture.inc';

$root = cgroup_fixture([
    '/proc/self/cgroup' => "0::/app\n",
    '/proc/self/mountinfo' => "30 1 0:26 / /sys/fs/cgroup rw shared:4 - cgroup2 cgroup2 rw\n",
    '/sys/fs/cgroup/app/cpu.max' => "50000 100000\n",
]);

TrueAsync\Test\cpu_limits($root, 8);
Async\cpu_usage();

$sum = 0;
for ($i = 0; $i < 2_000_000; $i++) {
    $sum += $i;
}

$usage = Async\cpu_usage();
var_dump(abs($usage['process_percent'] - min($usage['process_cores'] / 0.5 * 100, 100)) < 1e-9);

cgroup_fixture_remove($root);
?>
--EXPECT--
bool(true)
