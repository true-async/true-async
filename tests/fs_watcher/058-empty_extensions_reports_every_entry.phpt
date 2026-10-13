--TEST--
FileSystemWatcher: an empty extensions array reports every entry
--FILE--
<?php

use Async\FileSystemWatcher;
$dir = sys_get_temp_dir() . '/fsw_058_' . getmypid();
mkdir($dir);
$watcher = new FileSystemWatcher($dir, extensions: []);

touch("$dir/README");

foreach ($watcher as $event) {
    echo $event->filename, "\n";
    break;
}

$watcher->close();
unlink("$dir/README");
rmdir($dir);
?>
--EXPECT--
README
