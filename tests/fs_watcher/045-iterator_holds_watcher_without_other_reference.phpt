--TEST--
FileSystemWatcher: a foreach over a temporary watcher holds it while it waits
--FILE--
<?php

use Async\FileSystemWatcher;
use function Async\await;
use function Async\spawn;
use function Async\suspend;

$dir = sys_get_temp_dir() . '/fsw_045_' . getmypid();
mkdir($dir);
$parked = false;

$iterating = spawn(function () use ($dir, &$parked) {
    $parked = true;

    foreach (new FileSystemWatcher($dir) as $event) {
        echo "got {$event->filename}\n";
        break;
    }
});

while (!$parked) {
    suspend();
}

suspend();
touch("$dir/q");
await($iterating);
unlink("$dir/q");
rmdir($dir);
?>
--EXPECT--
got q
