--TEST--
FileSystemWatcher: of two parked iterators, the first woken takes the event and the other stays parked
--FILE--
<?php

use Async\FileSystemWatcher;
use Async\Channel;
use function Async\spawn;
use function Async\suspend;

$dir = sys_get_temp_dir() . '/fsw_019_' . getmypid();
mkdir($dir);
$watcher = new FileSystemWatcher($dir);
$received = new Channel(1);
$parked = 0;

foreach (['A', 'B'] as $name) {
    spawn(function () use ($watcher, $name, $received, &$parked) {
        $parked++;

        foreach ($watcher as $event) {
            $received->send("$name got {$event->filename}");
        }

        echo "$name ended\n";
    });
}

while ($parked < 2) {
    suspend();
}

suspend();
touch("$dir/x");
echo $received->recv(), "\n";
$watcher->close();
suspend();
unlink("$dir/x");
rmdir($dir);
?>
--EXPECT--
A got x
A ended
B ended
