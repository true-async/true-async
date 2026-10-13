--TEST--
available_parallelism(): a /proc/self/cgroup that cannot be read (a directory) gives no quota
--SKIPIF--
<?php if (PHP_OS_FAMILY !== 'Linux') die('skip the cgroup files are read on Linux only'); ?>
--FILE--
<?php

require __DIR__ . '/inc/cgroup_fixture.inc';

$root = cgroup_fixture([
    '/proc/self/cgroup/placeholder' => '',
    '/proc/self/mountinfo' => "30 1 0:26 / /sys/fs/cgroup rw shared:4 - cgroup2 cgroup2 rw\n",
    '/sys/fs/cgroup/cpu.max' => "100000 100000\n",
]);

TrueAsync\Test\cpu_limits($root, 8);
var_dump(Async\available_parallelism());

cgroup_fixture_remove($root);
?>
--EXPECT--
int(8)
