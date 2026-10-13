--TEST--
CpuSnapshot::now(): systemBusyNs lies between /proc/stat read just before and just after
--SKIPIF--
<?php
if (PHP_OS_FAMILY !== 'Linux') die('skip the host counters are read from /proc/stat on Linux only');
if (!is_numeric(trim((string) @shell_exec('getconf CLK_TCK 2>/dev/null')))) die('skip no getconf CLK_TCK');
?>
--FILE--
<?php

// Busy clock ticks of all CPUs, as the extension sums them. Idle has no bound: the kernel's iowait can decrease
// (proc(5)).
function busy_ticks(): int
{
    $fields = array_map('intval', array_slice(preg_split('/\s+/', strtok(file_get_contents('/proc/stat'), "\n")), 1, 8));
    [$user, $nice, $system, , , $irq, $softirq, $steal] = $fields;

    return $user + $nice + $system + $irq + $softirq + $steal;
}

$ns_per_tick = 1e9 / (int) trim(shell_exec('getconf CLK_TCK'));
// The extension converts through a double; a host's tick counts can lose the last digits there.
$slack = 1e-12;

$busy_before = busy_ticks();
$snapshot = Async\CpuSnapshot::now();
$busy_after = busy_ticks();

var_dump($busy_before * $ns_per_tick * (1 - $slack) <= $snapshot->systemBusyNs
    && $snapshot->systemBusyNs <= $busy_after * $ns_per_tick * (1 + $slack));
?>
--EXPECT--
bool(true)
