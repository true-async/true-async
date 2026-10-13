--TEST--
available_parallelism(): a cpu.max quota too large for 64 bits is no quota
--SKIPIF--
<?php if (PHP_OS_FAMILY !== 'Linux') die('skip the cgroup files are read on Linux only'); ?>
--FILE--
<?php

require __DIR__ . '/inc/cgroup_fixture.inc';

$root = cgroup_fixture([
    '/proc/self/cgroup' => "0::/app\n",
    '/proc/self/mountinfo' => "30 1 0:26 / /sys/fs/cgroup rw shared:4 - cgroup2 cgroup2 rw\n",
    '/sys/fs/cgroup/app/cpu.max' => "99999999999999999999999 100000\n",
]);

TrueAsync\Test\cpu_limits($root, 8);
var_dump(Async\available_parallelism());

cgroup_fixture_remove($root);
?>
--EXPECT--
int(8)
