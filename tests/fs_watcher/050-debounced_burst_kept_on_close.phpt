--TEST--
FileSystemWatcher: a debounced burst already read when the watcher closes comes out before foreach ends
--FILE--
<?php

use Async\FileSystemWatcher;

$dir = sys_get_temp_dir() . '/fsw_050_' . getmypid();
mkdir($dir);
$watcher = new FileSystemWatcher($dir, debounceMs: 60000);

// The removal's events and the watch's end are read together: the burst, then the close.
rmdir($dir);

foreach ($watcher as $event) {
    echo var_export($event->filename, true), ' renamed=', (int) $event->renamed, "\n";
}

var_dump($watcher->isClosed());
?>
--EXPECT--
NULL renamed=1
bool(true)
