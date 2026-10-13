--TEST--
FileSystemWatcher: foreach by reference throws an Error
--FILE--
<?php

use Async\FileSystemWatcher;
$dir = sys_get_temp_dir() . '/fsw_018_' . getmypid();
mkdir($dir);
$watcher = new FileSystemWatcher($dir);

try {
    foreach ($watcher as &$event) {
    }
} catch (Error $error) {
    echo $error->getMessage(), "\n";
}

$watcher->close();
rmdir($dir);
?>
--EXPECT--
Cannot iterate FileSystemWatcher by reference
