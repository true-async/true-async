--TEST--
FileSystemWatcher: $extensions filters without $debounceMs too, case-insensitively, with or without the dot
--FILE--
<?php

use Async\FileSystemWatcher;
$dir = sys_get_temp_dir() . '/fsw_038_' . getmypid();
mkdir($dir);
$watcher = new FileSystemWatcher($dir, extensions: ['.PHP']);

touch("$dir/a.txt");
touch("$dir/b.php");

foreach ($watcher as $event) {
    echo $event->filename, "\n";
    break;
}

$watcher->close();
unlink("$dir/a.txt");
unlink("$dir/b.php");
rmdir($dir);
?>
--EXPECT--
b.php
