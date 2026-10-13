--TEST--
FileSystemWatcher: a subdirectory moved out of a recursive watch is no longer watched
--FILE--
<?php

use Async\FileSystemWatcher;

$base = sys_get_temp_dir() . '/fsw_065_' . getmypid();
mkdir("$base/watched/sub", 0777, true);
mkdir("$base/outside");
$watcher = new FileSystemWatcher("$base/watched", recursive: true);

rename("$base/watched/sub", "$base/outside/sub");
touch("$base/outside/sub/after");
touch("$base/watched/marker");

foreach ($watcher as $event) {
    echo $event->filename, "\n";

    if ($event->filename === 'marker') {
        break;
    }
}

$watcher->close();
unlink("$base/outside/sub/after");
rmdir("$base/outside/sub");
rmdir("$base/outside");
unlink("$base/watched/marker");
rmdir("$base/watched");
rmdir($base);
?>
--EXPECT--
sub
marker
