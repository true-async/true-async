--TEST--
FileSystemWatcher: removing a subdirectory of a recursive watch keeps watching a sibling whose name starts with it
--FILE--
<?php

use Async\FileSystemWatcher;

$dir = sys_get_temp_dir() . '/fsw_066_' . getmypid();
mkdir("$dir/sub", 0777, true);
mkdir("$dir/sub2");
$watcher = new FileSystemWatcher($dir, recursive: true);

rmdir("$dir/sub");
touch("$dir/sub2/after");

foreach ($watcher as $event) {
    // Windows names entries with backslashes and also reports `sub2`'s times as a change, not a rename.
    if (!$event->renamed) {
        continue;
    }

    $filename = str_replace('\\', '/', $event->filename);
    echo $filename, "\n";

    if ($filename === 'sub2/after') {
        break;
    }
}

$watcher->close();
unlink("$dir/sub2/after");
rmdir("$dir/sub2");
rmdir($dir);
?>
--EXPECT--
sub
sub2/after
