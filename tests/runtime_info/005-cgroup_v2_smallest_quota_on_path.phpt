--TEST--
available_parallelism(): the smallest cgroup v2 quota along the path wins, here the leaf's
--SKIPIF--
<?php if (PHP_OS_FAMILY !== 'Linux') die('skip the cgroup files are read on Linux only'); ?>
--FILE--
<?php

require __DIR__ . '/inc/cgroup_fixture.inc';

$root = cgroup_fixture([
    '/proc/self/cgroup' => "0::/outer/inner\n",
    '/proc/self/mountinfo' => "30 1 0:26 / /sys/fs/cgroup rw,nosuid,nodev,noexec,relatime shared:4 - cgroup2 cgroup2 rw,nsdelegate\n",
    '/sys/fs/cgroup/outer/cpu.max' => "600000 100000\n",
    '/sys/fs/cgroup/outer/inner/cpu.max' => "200000 100000\n",
]);

TrueAsync\Test\cpu_limits($root, 8);
var_dump(Async\available_parallelism());

cgroup_fixture_remove($root);
?>
--EXPECT--
int(2)
