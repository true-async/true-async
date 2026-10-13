--TEST--
CpuSnapshot::now(): cpuCount is the count of online CPUs
--SKIPIF--
<?php
if (PHP_OS_FAMILY !== 'Linux') die('skip the online CPUs are read from /sys on Linux only');
if (@file_get_contents('/sys/devices/system/cpu/online') === false) die('skip no /sys/devices/system/cpu/online');
?>
--FILE--
<?php

// "0-3,6": the list sysconf(_SC_NPROCESSORS_ONLN) counts.
$online = 0;
foreach (explode(',', trim(file_get_contents('/sys/devices/system/cpu/online'))) as $range) {
    $bounds = explode('-', $range);
    $online += count($bounds) === 2 ? (int) $bounds[1] - (int) $bounds[0] + 1 : 1;
}

var_dump(Async\CpuSnapshot::now()->cpuCount === $online);
?>
--EXPECT--
bool(true)
