--TEST--
available_parallelism(): on a hybrid host the CPU controller's v1 line wins over the v2 line
--SKIPIF--
<?php if (PHP_OS_FAMILY !== 'Linux') die('skip the cgroup files are read on Linux only'); ?>
--FILE--
<?php

require __DIR__ . '/inc/cgroup_fixture.inc';

$root = cgroup_fixture([
    '/proc/self/cgroup' => "5:cpu,cpuacct:/user.slice\n0::/user.slice\n",
    '/proc/self/mountinfo' => "30 1 0:26 / /sys/fs/cgroup/unified rw shared:4 - cgroup2 cgroup2 rw\n40 1 0:35 / /sys/fs/cgroup/cpu,cpuacct rw shared:9 - cgroup cgroup rw,cpu,cpuacct\n",
    '/sys/fs/cgroup/unified/user.slice/cpu.max' => "100000 100000\n",
    '/sys/fs/cgroup/cpu,cpuacct/user.slice/cpu.cfs_quota_us' => "300000\n",
    '/sys/fs/cgroup/cpu,cpuacct/user.slice/cpu.cfs_period_us' => "100000\n",
]);

TrueAsync\Test\cpu_limits($root, 8);
var_dump(Async\available_parallelism());

cgroup_fixture_remove($root);
?>
--EXPECT--
int(3)
