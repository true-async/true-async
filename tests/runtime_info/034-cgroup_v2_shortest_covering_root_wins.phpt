--TEST--
available_parallelism(): of two cgroup v2 mounts that show the process's cgroup, the one with the shorter root is walked, so a quota above the longer root counts
--SKIPIF--
<?php if (PHP_OS_FAMILY !== 'Linux') die('skip the cgroup files are read on Linux only'); ?>
--FILE--
<?php

require __DIR__ . '/inc/cgroup_fixture.inc';

// Both mounts show the same hierarchy: /mnt/app is /mnt/whole/app/service.
$root = cgroup_fixture([
    '/proc/self/cgroup' => "0::/app/service\n",
    '/proc/self/mountinfo' => "31 1 0:26 /app/service /mnt/app rw shared:5 - cgroup2 cgroup2 rw\n"
        . "30 1 0:26 / /mnt/whole rw shared:4 - cgroup2 cgroup2 rw\n",
    '/mnt/app/cpu.max' => "max 100000\n",
    '/mnt/whole/app/service/cpu.max' => "max 100000\n",
    '/mnt/whole/app/cpu.max' => "200000 100000\n",
]);

TrueAsync\Test\cpu_limits($root, 8);
var_dump(Async\available_parallelism());

cgroup_fixture_remove($root);
?>
--EXPECT--
int(2)
