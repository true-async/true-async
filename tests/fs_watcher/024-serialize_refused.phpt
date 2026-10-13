--TEST--
FileSystemWatcher: serialize() throws an Exception
--FILE--
<?php

use Async\FileSystemWatcher;
$dir = sys_get_temp_dir() . '/fsw_024_' . getmypid();
mkdir($dir);
$watcher = new FileSystemWatcher($dir);

try {
    serialize($watcher);
} catch (Exception $exception) {
    echo $exception->getMessage(), "\n";
}

$watcher->close();
rmdir($dir);
?>
--EXPECT--
Serialization of 'Async\FileSystemWatcher' is not allowed
