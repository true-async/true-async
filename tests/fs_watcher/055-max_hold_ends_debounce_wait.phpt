--TEST--
FileSystemWatcher: maxHoldMs delivers a debounced burst before its debounceMs of quiet
--FILE--
<?php

use Async\FileSystemWatcher;
$dir = sys_get_temp_dir() . '/fsw_055_' . getmypid();
mkdir($dir);
// Without maxHoldMs the burst would wait 600 s, past the test's timeout.
$watcher = new FileSystemWatcher($dir, debounceMs: 600000, maxHoldMs: 10);

touch("$dir/a");

foreach ($watcher as $event) {
    var_dump($event->filename, $event->renamed);
    break;
}

$watcher->close();
unlink("$dir/a");
rmdir($dir);
?>
--EXPECT--
NULL
bool(true)
