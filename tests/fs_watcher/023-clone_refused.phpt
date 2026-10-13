--TEST--
FileSystemWatcher: clone throws an Error
--FILE--
<?php

use Async\FileSystemWatcher;
$dir = sys_get_temp_dir() . '/fsw_023_' . getmypid();
mkdir($dir);
$watcher = new FileSystemWatcher($dir);

try {
    $copy = clone $watcher;
} catch (Error $error) {
    echo $error->getMessage(), "\n";
}

$watcher->close();
rmdir($dir);
?>
--EXPECT--
Trying to clone an uncloneable object of class Async\FileSystemWatcher
