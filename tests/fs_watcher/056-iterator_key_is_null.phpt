--TEST--
FileSystemWatcher: foreach gives a null key
--FILE--
<?php

use Async\FileSystemWatcher;
$dir = sys_get_temp_dir() . '/fsw_056_' . getmypid();
mkdir($dir);
$watcher = new FileSystemWatcher($dir);

touch("$dir/a");

foreach ($watcher as $key => $event) {
    var_dump($key);
    break;
}

$watcher->close();
unlink("$dir/a");
rmdir($dir);
?>
--EXPECT--
NULL
