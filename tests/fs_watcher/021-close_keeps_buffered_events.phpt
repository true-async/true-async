--TEST--
FileSystemWatcher: events buffered before close() still come out, then foreach ends
--FILE--
<?php

use Async\FileSystemWatcher;
$dir = sys_get_temp_dir() . '/fsw_021_' . getmypid();
mkdir($dir);
$watcher = new FileSystemWatcher($dir);

touch("$dir/a");
touch("$dir/b");

// The first wait reads both events from the kernel's queue.
foreach ($watcher as $event) {
    echo $event->filename, "\n";
    $watcher->close();
}

echo "ended\n";
unlink("$dir/a");
unlink("$dir/b");
rmdir($dir);
?>
--EXPECT--
a
b
ended
