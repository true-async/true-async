--TEST--
FileSystemWatcher: a forked child adds the watches again on its own inotify descriptor and gets its events
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
use function Async\suspend;

$dir = sys_get_temp_dir() . '/fsw_044_' . getmypid();
mkdir($dir);
$watcher = new FileSystemWatcher($dir);
$pid = pcntl_fork();

if ($pid === 0) {
    $parked = false;

    // Its first wait adds the watches again; a change made before it is not seen.
    $iterating = spawn(function () use ($watcher, &$parked) {
        $parked = true;

        foreach ($watcher as $event) {
            echo "child got {$event->filename}\n";
            break;
        }
    });

    while (!$parked) {
        suspend();
    }

    suspend();
    touch("$dir/child");
    await($iterating);
    exit(0);
}

pcntl_waitpid($pid, $status);
touch("$dir/parent");

foreach ($watcher as $event) {
    echo "parent got {$event->filename}\n";

    if ($event->filename === 'parent') {
        break;
    }
}

$watcher->close();
unlink("$dir/child");
unlink("$dir/parent");
rmdir($dir);
echo "parent: child exited with ", pcntl_wexitstatus($status), "\n";
?>
--EXPECT--
child got child
parent got child
parent got parent
parent: child exited with 0
