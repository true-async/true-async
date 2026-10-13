--TEST--
cpu_usage(): system_percent is above 0 and at most 100 when the host counted busy ticks between the two calls
--SKIPIF--
<?php if (PHP_OS_FAMILY !== 'Linux') die('skip the host counters are read from /proc/stat on Linux only'); ?>
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

Async\cpu_usage();
$busy_after_first = busy_ticks();

// Runs until the host has counted two busy ticks after the first call. The deadline only stops a host whose
// counters stand still (a sandbox that fakes /proc/stat) from hanging the test.
$deadline = hrtime(true) + 10_000_000_000;
while (busy_ticks() - $busy_after_first < 2) {
    if (hrtime(true) > $deadline) {
        die("the host counted no busy ticks\n");
    }
}

$usage = Async\cpu_usage();
var_dump($usage['system_percent'] > 0 && $usage['system_percent'] <= 100);
?>
--EXPECT--
bool(true)
