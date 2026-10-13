--TEST--
cpu_usage(): interval_sec lies between the hrtime() spans around the two calls
--FILE--
<?php

$before_first = hrtime(true);
Async\cpu_usage();
$after_first = hrtime(true);

$before_second = hrtime(true);
$usage = Async\cpu_usage();
$after_second = hrtime(true);

// Both read the same monotonic clock; the slack covers the conversion to seconds.
$interval_ns = $usage['interval_sec'] * 1e9;
var_dump($before_second - $after_first - 1 <= $interval_ns && $interval_ns <= $after_second - $before_first + 1);
?>
--EXPECT--
bool(true)
