--TEST--
available_parallelism(): a process confined to one CPU by its affinity mask gets 1
--SKIPIF--
<?php
if (PHP_OS_FAMILY !== 'Linux') die('skip taskset is Linux only');
exec('command -v taskset', $output, $status);
if ($status !== 0) die('skip taskset is not installed');
?>
--FILE--
<?php

$command = 'taskset -c 0 ' . getenv('TEST_PHP_EXECUTABLE_ESCAPED') . ' ' . getenv('TEST_PHP_EXTRA_ARGS')
    . ' -r ' . escapeshellarg('echo Async\available_parallelism(), "\n";');
passthru($command);
?>
--EXPECT--
1
