--TEST--
FileSystemWatcher: getAwaitingInfo() of a parked iterator names the watcher
--FILE--
<?php

use Async\FileSystemWatcher;
use function Async\spawn;
use function Async\suspend;

$dir = sys_get_temp_dir() . '/fsw_046_' . getmypid();
mkdir($dir);
$watcher = new FileSystemWatcher($dir);
$parked = false;

$iterating = spawn(function () use ($watcher, &$parked) {
    $parked = true;

    foreach ($watcher as $event) {
    }
});

while (!$parked) {
    suspend();
}

suspend();
echo implode("\n", $iterating->getAwaitingInfo()) === "foreach: FileSystemWatcher($dir)" ? "named\n" : "not named\n";
$watcher->close();
rmdir($dir);
?>
--EXPECT--
named
