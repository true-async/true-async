--TEST--
available_parallelism(): a cgroup v1 quota without its cpu.cfs_period_us is no quota
--SKIPIF--
<?php if (PHP_OS_FAMILY !== 'Linux') die('skip the cgroup files are read on Linux only'); ?>
--FILE--
<?php

require __DIR__ . '/inc/cgroup_fixture.inc';

$root = cgroup_fixture([
    '/proc/self/cgroup' => "4:cpu:/app\n",
    '/proc/self/mountinfo' => "40 1 0:35 / /sys/fs/cgroup/cpu rw shared:9 - cgroup cgroup rw,cpu\n",
    '/sys/fs/cgroup/cpu/app/cpu.cfs_quota_us' => "100000\n",
]);

TrueAsync\Test\cpu_limits($root, 8);
var_dump(Async\available_parallelism());

cgroup_fixture_remove($root);
?>
--EXPECT--
int(8)
