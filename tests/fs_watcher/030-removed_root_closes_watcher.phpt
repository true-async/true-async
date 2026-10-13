--TEST--
FileSystemWatcher: removing the watched directory closes the watcher, and foreach ends after the events already read
--FILE--
<?php

use Async\FileSystemWatcher;
$dir = sys_get_temp_dir() . '/fsw_030_' . getmypid();
mkdir($dir);
$watcher = new FileSystemWatcher($dir, coalesce: false);

rmdir($dir);

foreach ($watcher as $event) {
    echo var_export($event->filename, true), ' renamed=', (int) $event->renamed, "\n";
}

var_dump($watcher->isClosed());
?>
--EXPECTF--
%s renamed=1
bool(true)
