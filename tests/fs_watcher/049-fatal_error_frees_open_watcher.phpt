--TEST--
FileSystemWatcher: a fatal error frees an open watcher with an armed debounce without its destructor, and the shutdown finds it closed
--INI--
memory_limit=16M
--FILE--
<?php

use Async\Channel;
use Async\FileSystemWatcher;
use function Async\spawn;
use function Async\suspend;

$dir = sys_get_temp_dir() . '/fsw_049_' . getmypid();
mkdir($dir);
register_shutdown_function(function () use ($dir) {
    unlink("$dir/a");
    rmdir($dir);
});
$watcher = new FileSystemWatcher($dir, debounceMs: 60000);
$seen = new Channel(1);
$parked = false;

// The second watcher's event comes from the same read that arms the first one's debounce timer.
spawn(function () use ($dir, $seen, &$parked) {
    $parked = true;

    foreach (new FileSystemWatcher($dir) as $event) {
        $seen->send($event->filename);
        break;
    }
});

while (!$parked) {
    suspend();
}

suspend();
touch("$dir/a");
$seen->recv();
$exhausted = str_repeat('x', 64 << 20);
?>
--EXPECTF--
Fatal error: Allowed memory size of %d bytes exhausted %s
