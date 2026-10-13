--TEST--
FileSystemWatcher: a watch on a file names the file itself by its basename
--FILE--
<?php

use Async\FileSystemWatcher;
$file = sys_get_temp_dir() . '/fsw_017_' . getmypid() . '.txt';
file_put_contents($file, '1');
$watcher = new FileSystemWatcher($file);

file_put_contents($file, '2');

foreach ($watcher as $event) {
    var_dump($event->path === $file, $event->filename === basename($file), $event->changed);
    break;
}

$watcher->close();
unlink($file);
?>
--EXPECT--
bool(true)
bool(true)
bool(true)
