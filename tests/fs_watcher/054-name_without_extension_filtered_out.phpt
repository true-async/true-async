--TEST--
FileSystemWatcher: an entry without an extension does not pass an extension filter
--FILE--
<?php

use Async\FileSystemWatcher;

$dir = sys_get_temp_dir() . '/fsw_054_' . getmypid();
mkdir($dir);
$watcher = new FileSystemWatcher($dir, extensions: ['txt']);

touch("$dir/README");
touch("$dir/a.txt");

foreach ($watcher as $event) {
    echo $event->filename, "\n";
    break;
}

$watcher->close();
unlink("$dir/README");
unlink("$dir/a.txt");
rmdir($dir);
?>
--EXPECT--
a.txt
