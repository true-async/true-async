--TEST--
FileSystemWatcher: a second __construct() throws an Error and keeps the first watch
--FILE--
<?php

use Async\FileSystemWatcher;
$dir = sys_get_temp_dir() . '/fsw_029_' . getmypid();
mkdir($dir);
$watcher = new FileSystemWatcher($dir);

try {
    $watcher->__construct($dir);
} catch (Error $error) {
    echo $error->getMessage(), "\n";
}

touch("$dir/a");

foreach ($watcher as $event) {
    echo $event->filename, "\n";
    break;
}

$watcher->close();
unlink("$dir/a");
rmdir($dir);
?>
--EXPECT--
Cannot call constructor twice
a
