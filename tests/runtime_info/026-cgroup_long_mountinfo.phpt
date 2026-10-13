--TEST--
available_parallelism(): a mountinfo longer than the first read buffer is read whole
--SKIPIF--
<?php if (PHP_OS_FAMILY !== 'Linux') die('skip the cgroup files are read on Linux only'); ?>
--FILE--
<?php

require __DIR__ . '/inc/cgroup_fixture.inc';

// 300 mounts of other file systems before the cgroup2 one, about 20 KiB.
$other_mounts = '';
for ($i = 0; $i < 300; $i++) {
    $other_mounts .= "$i 1 8:1 /data/volume$i /mnt/volume$i rw,relatime shared:$i - ext4 /dev/sda1 rw\n";
}

$root = cgroup_fixture([
    '/proc/self/cgroup' => "0::/app\n",
    '/proc/self/mountinfo' => $other_mounts . "30 1 0:26 / /sys/fs/cgroup rw shared:4 - cgroup2 cgroup2 rw\n",
    '/sys/fs/cgroup/app/cpu.max' => "200000 100000\n",
]);

TrueAsync\Test\cpu_limits($root, 8);
var_dump(Async\available_parallelism());

cgroup_fixture_remove($root);
?>
--EXPECT--
int(2)
