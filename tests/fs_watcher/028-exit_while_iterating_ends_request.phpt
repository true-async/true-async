--TEST--
FileSystemWatcher: exit() while a coroutine iterates cancels it and ends the request
--FILE--
<?php

use Async\FileSystemWatcher;
use function Async\spawn;
use function Async\suspend;

$dir = sys_get_temp_dir() . '/fsw_028_' . getmypid();
mkdir($dir);
register_shutdown_function(fn() => rmdir($dir));
$watcher = new FileSystemWatcher($dir);
$parked = false;

spawn(function () use ($watcher, &$parked) {
    try {
        $parked = true;

        foreach ($watcher as $event) {
        }
    } finally {
        echo "finally\n";
    }
});

while (!$parked) {
    suspend();
}

echo "exit\n";
exit(0);
?>
--EXPECT--
exit
finally
