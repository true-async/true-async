--TEST--
available_parallelism(): a mount point with a space, escaped as \040 in mountinfo, is read
--SKIPIF--
<?php if (PHP_OS_FAMILY !== 'Linux') die('skip the cgroup files are read on Linux only'); ?>
--FILE--
<?php

require __DIR__ . '/inc/cgroup_fixture.inc';

$root = cgroup_fixture([
    '/proc/self/cgroup' => "0::/app\n",
    '/proc/self/mountinfo' => "30 1 0:26 / /sys/fs/cgroup\\040v2 rw shared:4 - cgroup2 cgroup2 rw\n",
    '/sys/fs/cgroup v2/app/cpu.max' => "200000 100000\n",
]);

TrueAsync\Test\cpu_limits($root, 8);
var_dump(Async\available_parallelism());

cgroup_fixture_remove($root);
?>
--EXPECT--
int(2)
