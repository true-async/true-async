--TEST--
available_parallelism(): a root whose /proc/self/cgroup path does not fit MAXPATHLEN gives no quota
--SKIPIF--
<?php if (PHP_OS_FAMILY !== 'Linux') die('skip the cgroup files are read on Linux only'); ?>
--FILE--
<?php

TrueAsync\Test\cpu_limits('/' . str_repeat('a', 5000), 8);
var_dump(Async\available_parallelism());
?>
--EXPECT--
int(8)
