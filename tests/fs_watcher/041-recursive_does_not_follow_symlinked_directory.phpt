--TEST--
FileSystemWatcher: a recursive watch does not follow a symlinked directory
--FILE--
<?php

use Async\FileSystemWatcher;
$base = sys_get_temp_dir() . '/fsw_041_' . getmypid();
mkdir("$base/watched", 0777, true);
mkdir("$base/target");
symlink("$base/target", "$base/watched/link");
$watcher = new FileSystemWatcher("$base/watched", recursive: true);

touch("$base/target/hidden");
touch("$base/watched/seen");

foreach ($watcher as $event) {
    echo $event->filename, "\n";
    break;
}

$watcher->close();
unlink("$base/target/hidden");
unlink("$base/watched/seen");
// A symlink to a directory is removed with rmdir() on Windows.
PHP_OS_FAMILY === 'Windows' ? rmdir("$base/watched/link") : unlink("$base/watched/link");
rmdir("$base/target");
rmdir("$base/watched");
rmdir($base);
?>
--EXPECT--
seen
