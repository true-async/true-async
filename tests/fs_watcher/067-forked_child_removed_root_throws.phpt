--TEST--
FileSystemWatcher: a forked child whose watched directory is gone when it watches again gets the monitoring error
--EXTENSIONS--
pcntl
--SKIPIF--
<?php
if (PHP_OS_FAMILY === 'Windows') echo "skip Unix-only test";
?>
--FILE--
<?php

use Async\FileSystemWatcher;

$dir = sys_get_temp_dir() . '/fsw_067_' . getmypid();
mkdir($dir);
$watcher = new FileSystemWatcher($dir);
$pid = pcntl_fork();

if ($pid === 0) {
    rmdir($dir);

    // The foreach's first wait adds the watch again, which the kernel refuses.
    try {
        foreach ($watcher as $event) {
            echo "child: not reached\n";
        }
    } catch (Async\AsyncException $exception) {
        echo "child: ", $exception->getMessage(), "\n";
    }

    exit(0);
}

pcntl_waitpid($pid, $status);
$watcher->close();
echo "parent: child exited with ", pcntl_wexitstatus($status), "\n";
?>
--EXPECT--
child: Filesystem monitoring error: No such file or directory
parent: child exited with 0
