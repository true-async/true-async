--TEST--
available_parallelism(): a cpu.max too long for its buffer is no quota, not the number cut at the buffer's end
--SKIPIF--
<?php if (PHP_OS_FAMILY !== 'Linux') die('skip the cgroup files are read on Linux only'); ?>
--FILE--
<?php

require __DIR__ . '/inc/cgroup_fixture.inc';

// Cut at 63 bytes, the period would read as 10000 and the quota as 20 CPUs.
$root = cgroup_fixture([
    '/proc/self/cgroup' => "0::/app\n",
    '/proc/self/mountinfo' => "30 1 0:26 / /sys/fs/cgroup rw shared:4 - cgroup2 cgroup2 rw\n",
    '/sys/fs/cgroup/app/cpu.max' => str_repeat('0', 50) . "200000 100000\n",
]);

TrueAsync\Test\cpu_limits($root, 32);
var_dump(Async\available_parallelism());

cgroup_fixture_remove($root);
?>
--EXPECT--
int(32)
