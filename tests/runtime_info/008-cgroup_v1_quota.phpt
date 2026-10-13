--TEST--
available_parallelism(): a cgroup v1 quota of 1.5 CPUs gives 2
--SKIPIF--
<?php if (PHP_OS_FAMILY !== 'Linux') die('skip the cgroup files are read on Linux only'); ?>
--FILE--
<?php

require __DIR__ . '/inc/cgroup_fixture.inc';

$root = cgroup_fixture([
    '/proc/self/cgroup' => "5:cpu,cpuacct:/docker/abc\n4:memory:/docker/abc\n",
    '/proc/self/mountinfo' => "40 30 0:35 / /sys/fs/cgroup/cpu,cpuacct rw,nosuid shared:9 - cgroup cgroup rw,cpu,cpuacct\n41 30 0:36 / /sys/fs/cgroup/memory rw,nosuid shared:10 - cgroup cgroup rw,memory\n",
    '/sys/fs/cgroup/cpu,cpuacct/docker/abc/cpu.cfs_quota_us' => "150000\n",
    '/sys/fs/cgroup/cpu,cpuacct/docker/abc/cpu.cfs_period_us' => "100000\n",
    '/sys/fs/cgroup/cpu,cpuacct/docker/cpu.cfs_quota_us' => "-1\n",
    '/sys/fs/cgroup/cpu,cpuacct/docker/cpu.cfs_period_us' => "100000\n",
]);

TrueAsync\Test\cpu_limits($root, 8);
var_dump(Async\available_parallelism());

cgroup_fixture_remove($root);
?>
--EXPECT--
int(2)
