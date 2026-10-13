--TEST--
FileSystemWatcher: an extension of 16 or more characters filters too
--FILE--
<?php

use Async\FileSystemWatcher;
$dir = sys_get_temp_dir() . '/fsw_037_' . getmypid();
mkdir($dir);
$watcher = new FileSystemWatcher($dir, extensions: ['configuration_backup']);

touch("$dir/a.txt");
touch("$dir/b.configuration_backup");

foreach ($watcher as $event) {
    echo $event->filename, "\n";
    break;
}

$watcher->close();
unlink("$dir/a.txt");
unlink("$dir/b.configuration_backup");
rmdir($dir);
?>
--EXPECT--
b.configuration_backup
