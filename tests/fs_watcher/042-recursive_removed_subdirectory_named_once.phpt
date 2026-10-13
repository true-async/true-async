--TEST--
FileSystemWatcher: a removed subdirectory of a recursive watch is named by its parent only, not by its own watch
--FILE--
<?php

use Async\FileSystemWatcher;
$dir = sys_get_temp_dir() . '/fsw_042_' . getmypid();
mkdir("$dir/sub", 0777, true);
$watcher = new FileSystemWatcher($dir, recursive: true, coalesce: false);

rmdir("$dir/sub");
touch("$dir/after");

foreach ($watcher as $event) {
    echo $event->filename, "\n";

    if ($event->filename === 'after') {
        break;
    }
}

$watcher->close();
unlink("$dir/after");
rmdir($dir);
?>
--EXPECT--
sub
after
