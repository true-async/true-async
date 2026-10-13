--TEST--
FileSystemWatcher: an entry whose name ends in a dot does not pass an extension filter
--SKIPIF--
<?php
if (PHP_OS_FAMILY === 'Windows') echo "skip Windows drops a name's trailing dot";
?>
--FILE--
<?php

use Async\FileSystemWatcher;

$dir = sys_get_temp_dir() . '/fsw_070_' . getmypid();
mkdir($dir);
$watcher = new FileSystemWatcher($dir, extensions: ['txt']);

touch("$dir/notes.");
touch("$dir/a.txt");

foreach ($watcher as $event) {
    echo $event->filename, "\n";
    break;
}

$watcher->close();
unlink("$dir/notes.");
unlink("$dir/a.txt");
rmdir($dir);
?>
--EXPECT--
a.txt
