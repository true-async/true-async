--TEST--
FileSystemWatcher: await() refuses it, it is Awaitable, not Completable
--FILE--
<?php

use Async\FileSystemWatcher;
$dir = sys_get_temp_dir() . '/fsw_022_' . getmypid();
mkdir($dir);
$watcher = new FileSystemWatcher($dir);

try {
    Async\await($watcher);
} catch (TypeError $error) {
    echo get_class($error), "\n";
}

$watcher->close();
rmdir($dir);
?>
--EXPECT--
TypeError
