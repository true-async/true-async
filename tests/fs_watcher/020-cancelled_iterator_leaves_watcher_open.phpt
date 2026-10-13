--TEST--
FileSystemWatcher: a cancel of the iterating coroutine leaves foreach, and a later foreach gets later events
--FILE--
<?php

use Async\FileSystemWatcher;
use function Async\await;
use function Async\spawn;
use function Async\suspend;

$dir = sys_get_temp_dir() . '/fsw_020_' . getmypid();
mkdir($dir);
$watcher = new FileSystemWatcher($dir);
$parked = false;

$iterating = spawn(function () use ($watcher, &$parked) {
    $parked = true;

    foreach ($watcher as $event) {
        echo "not reached\n";
    }
});

while (!$parked) {
    suspend();
}

suspend();
$iterating->cancel();

try {
    await($iterating);
} catch (Async\AsyncCancellation $cancellation) {
    echo get_class($cancellation), "\n";
}

var_dump($watcher->isClosed());
touch("$dir/y");

foreach ($watcher as $event) {
    echo $event->filename, "\n";
    break;
}

$watcher->close();
unlink("$dir/y");
rmdir($dir);
?>
--EXPECT--
Async\AsyncCancellation
bool(false)
y
