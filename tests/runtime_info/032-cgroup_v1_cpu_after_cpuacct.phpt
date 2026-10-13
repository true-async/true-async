--TEST--
available_parallelism(): cgroup v1 with cpuacct mounted apart reads the cpu line and the mount whose options list cpu second
--SKIPIF--
<?php if (PHP_OS_FAMILY !== 'Linux') die('skip the cgroup files are read on Linux only'); ?>
--FILE--
<?php

require __DIR__ . '/inc/cgroup_fixture.inc';

$root = cgroup_fixture([
    '/proc/self/cgroup' => "4:cpuacct:/acct.slice\n3:cpu:/app.slice\n",
    '/proc/self/mountinfo' => "40 1 0:35 / /sys/fs/cgroup/cpuacct rw shared:9 - cgroup cgroup rw,cpuacct\n"
        . "41 1 0:36 / /sys/fs/cgroup/cpu rw shared:10 - cgroup cgroup rw,cpu\n",
    '/sys/fs/cgroup/cpuacct/acct.slice/cpu.cfs_quota_us' => "100000\n",
    '/sys/fs/cgroup/cpuacct/acct.slice/cpu.cfs_period_us' => "100000\n",
    '/sys/fs/cgroup/cpu/app.slice/cpu.cfs_quota_us' => "300000\n",
    '/sys/fs/cgroup/cpu/app.slice/cpu.cfs_period_us' => "100000\n",
]);

TrueAsync\Test\cpu_limits($root, 8);
var_dump(Async\available_parallelism());

cgroup_fixture_remove($root);
?>
--EXPECT--
int(3)
