--TEST--
available_parallelism(): a cgroup v2 quota of 2.5 CPUs above the process's cgroup gives 3
--SKIPIF--
<?php if (PHP_OS_FAMILY !== 'Linux') die('skip the cgroup files are read on Linux only'); ?>
--FILE--
<?php

require __DIR__ . '/inc/cgroup_fixture.inc';

$root = cgroup_fixture([
    '/proc/self/cgroup' => "0::/kubepods/pod/container\n",
    '/proc/self/mountinfo' => "30 1 0:26 / /sys/fs/cgroup rw,nosuid,nodev,noexec,relatime shared:4 - cgroup2 cgroup2 rw,nsdelegate\n",
    '/sys/fs/cgroup/kubepods/cpu.max' => "max 100000\n",
    '/sys/fs/cgroup/kubepods/pod/cpu.max' => "250000 100000\n",
    '/sys/fs/cgroup/kubepods/pod/container/cpu.max' => "max 100000\n",
]);

TrueAsync\Test\cpu_limits($root, 8);
var_dump(Async\available_parallelism());

cgroup_fixture_remove($root);
?>
--EXPECT--
int(3)
