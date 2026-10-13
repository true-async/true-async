--TEST--
FileSystemWatcher: two watchers on one directory share its watch, and closing one leaves the other watching
--FILE--
<?php

use Async\FileSystemWatcher;
$dir = sys_get_temp_dir() . '/fsw_043_' . getmypid();
mkdir("$dir/sub", 0777, true);
$recursive = new FileSystemWatcher($dir, recursive: true);
$plain = new FileSystemWatcher("$dir/sub");

$recursive->close();
touch("$dir/sub/a");

foreach ($plain as $event) {
    echo $event->filename, "\n";
    break;
}

$plain->close();
unlink("$dir/sub/a");
rmdir("$dir/sub");
rmdir($dir);
?>
--EXPECT--
a
