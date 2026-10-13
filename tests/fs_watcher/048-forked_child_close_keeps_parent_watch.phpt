--TEST--
FileSystemWatcher: a forked child that closes the watcher before it waits leaves the parent's watch in place
--EXTENSIONS--
pcntl
--SKIPIF--
<?php
if (PHP_OS_FAMILY === 'Windows') echo "skip Unix-only test";
?>
--FILE--
<?php

use Async\FileSystemWatcher;

$dir = sys_get_temp_dir() . '/fsw_048_' . getmypid();
mkdir($dir);
$watcher = new FileSystemWatcher($dir);
$pid = pcntl_fork();

if ($pid === 0) {
    $watcher->close();
    exit(0);
}

pcntl_waitpid($pid, $status);
touch("$dir/after");

foreach ($watcher as $event) {
    echo $event->filename, "\n";
    break;
}

$watcher->close();
unlink("$dir/after");
rmdir($dir);
?>
--EXPECT--
after
