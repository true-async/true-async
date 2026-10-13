--TEST--
FileSystemWatcher: a failed poll closes every open watcher with the monitoring error
--FILE--
<?php

use Async\FileSystemWatcher;
use TrueAsync\Test;

$base = sys_get_temp_dir() . '/fsw_063_' . getmypid();
mkdir("$base/a", 0777, true);
mkdir("$base/b");
$first = new FileSystemWatcher("$base/a");
$second = new FileSystemWatcher("$base/b");
$firstEvents = $first->getIterator();
$secondEvents = $second->getIterator();

touch("$base/a/x");
Test\fail_io('complete');

foreach ([$firstEvents, $secondEvents] as $events) {
    try {
        foreach ($events as $event) {
            echo "not reached\n";
        }
    } catch (Async\AsyncException $exception) {
        echo $exception->getMessage(), "\n";
    }
}

var_dump($first->isClosed(), $second->isClosed());
unlink("$base/a/x");
rmdir("$base/a");
rmdir("$base/b");
rmdir($base);
?>
--EXPECT--
Filesystem monitoring error: Input/output error
Filesystem monitoring error: Input/output error
bool(true)
bool(true)
