--TEST--
FileSystemWatcher: a debounced burst that comes out on close carries the burst's flags, not inverted ones
--FILE--
<?php

use Async\FileSystemWatcher;

$dir = sys_get_temp_dir() . '/fsw_062_' . getmypid();
mkdir($dir);
$watcher = new FileSystemWatcher($dir, debounceMs: 60000);

// A removal renames and changes nothing; the burst is read together with the watch's end.
rmdir($dir);

foreach ($watcher as $event) {
    echo var_export($event->filename, true), ' renamed=', (int) $event->renamed, ' changed=', (int) $event->changed, "\n";
}
?>
--EXPECT--
NULL renamed=1 changed=0
