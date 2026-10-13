--TEST--
available_parallelism(): a cgroup v2 line with no cgroup2 mount has no quota
--SKIPIF--
<?php if (PHP_OS_FAMILY !== 'Linux') die('skip the cgroup files are read on Linux only'); ?>
--FILE--
<?php

require __DIR__ . '/inc/cgroup_fixture.inc';

$root = cgroup_fixture([
    '/proc/self/cgroup' => "0::/app\n",
    '/proc/self/mountinfo' => "22 1 8:1 / / rw shared:1 - ext4 /dev/sda1 rw\n",
    '/sys/fs/cgroup/app/cpu.max' => "100000 100000\n",
]);

TrueAsync\Test\cpu_limits($root, 8);
var_dump(Async\available_parallelism());

cgroup_fixture_remove($root);
?>
--EXPECT--
int(8)
