--TEST--
FileSystemWatcher: a forked child adds the watch of a watched file again and gets its change
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

$dir = sys_get_temp_dir() . '/fsw_069_' . getmypid();
mkdir($dir);
touch("$dir/watched");
$watcher = new FileSystemWatcher("$dir/watched");
$pid = pcntl_fork();

if ($pid === 0) {
    // A new watch in the child first adds the watch of the parent's watcher again.
    $other = new FileSystemWatcher($dir);
    file_put_contents("$dir/watched", 'child');

    await(spawn(function () use ($watcher) {
        foreach ($watcher as $event) {
            echo "child got changed=", (int) $event->changed, "\n";
            break;
        }
    }));
    exit(0);
}

pcntl_waitpid($pid, $status);
$watcher->close();
unlink("$dir/watched");
rmdir($dir);
echo "parent: child exited with ", pcntl_wexitstatus($status), "\n";
?>
--EXPECT--
child got changed=1
parent: child exited with 0
