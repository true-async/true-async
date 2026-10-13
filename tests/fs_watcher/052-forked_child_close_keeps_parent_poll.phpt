--TEST--
FileSystemWatcher: on the Poll queue, a forked child that closes the watcher before it waits leaves the parent's wait in place
--EXTENSIONS--
pcntl
--SKIPIF--
<?php
if (PHP_OS_FAMILY === 'Windows') echo "skip Unix-only test";
?>
--FILE--
<?php

use Async\FileSystemWatcher;
use TrueAsync\Test;

Test\reactor_use_poll_queue();

$dir = sys_get_temp_dir() . '/fsw_052_' . getmypid();
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
