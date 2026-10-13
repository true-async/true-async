--TEST--
FileSystemWatcher: a forked child adds the watches of every watcher again, not only the first
--EXTENSIONS--
pcntl
--SKIPIF--
<?php
if (PHP_OS_FAMILY === 'Windows') echo "skip Unix-only test";
?>
--FILE--
<?php

use Async\FileSystemWatcher;
use function Async\await;
use function Async\spawn;

$base = sys_get_temp_dir() . '/fsw_068_' . getmypid();
mkdir("$base/a", 0777, true);
mkdir("$base/b");
mkdir("$base/c");
$first = new FileSystemWatcher("$base/a");
$second = new FileSystemWatcher("$base/b");
$pid = pcntl_fork();

if ($pid === 0) {
    // A new watch in the child first adds the watches of the parent's watchers again.
    $third = new FileSystemWatcher("$base/c");
    touch("$base/b/child");

    await(spawn(function () use ($second) {
        foreach ($second as $event) {
            echo "child got {$event->filename}\n";
            break;
        }
    }));
    exit(0);
}

pcntl_waitpid($pid, $status);
$first->close();
$second->close();
unlink("$base/b/child");
rmdir("$base/a");
rmdir("$base/b");
rmdir("$base/c");
rmdir($base);
echo "parent: child exited with ", pcntl_wexitstatus($status), "\n";
?>
--EXPECT--
child got child
parent: child exited with 0
