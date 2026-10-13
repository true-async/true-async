--TEST--
FileSystemWatcher: a failed poll of the inotify descriptor closes the watcher and foreach throws the error once
--FILE--
<?php

use Async\FileSystemWatcher;
use TrueAsync\Test;

$dir = sys_get_temp_dir() . '/fsw_039_' . getmypid();
mkdir($dir);
$watcher = new FileSystemWatcher($dir);

touch("$dir/a");
Test\fail_io('complete');

try {
    foreach ($watcher as $event) {
        echo "not reached\n";
    }
} catch (Async\AsyncException $exception) {
    echo $exception->getMessage(), "\n";
}

var_dump($watcher->isClosed());

foreach ($watcher as $event) {
    echo "not reached\n";
}

echo "second foreach ended\n";
unlink("$dir/a");
rmdir($dir);
?>
--EXPECT--
Filesystem monitoring error: Input/output error
bool(true)
second foreach ended
