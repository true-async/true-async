--TEST--
FileSystemWatcher: a watched file replaced by a rename over it closes the watcher
--FILE--
<?php

use Async\FileSystemWatcher;
$file = sys_get_temp_dir() . '/fsw_047_' . getmypid() . '.txt';
file_put_contents($file, '1');
file_put_contents("$file.tmp", '2');
$watcher = new FileSystemWatcher($file);

rename("$file.tmp", $file);

foreach ($watcher as $event) {
}

var_dump($watcher->isClosed());
unlink($file);
?>
--EXPECT--
bool(true)
