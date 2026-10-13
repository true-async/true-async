--TEST--
FileSystemWatcher: an entry with a long name changed before another is named byte for byte
--FILE--
<?php

use Async\FileSystemWatcher;
$dir = sys_get_temp_dir() . '/fsw_053_' . getmypid();
mkdir($dir);
// 60 characters: on Windows the next record's offset follows the name, which has no terminator.
$long = str_repeat('n', 56) . '.txt';
$watcher = new FileSystemWatcher($dir);

touch("$dir/$long");
touch("$dir/after");

foreach ($watcher as $event) {
    var_dump($event->filename === $long || $event->filename === 'after');

    if ($event->filename === 'after') {
        break;
    }
}

$watcher->close();
unlink("$dir/$long");
unlink("$dir/after");
rmdir($dir);
?>
--EXPECT--
bool(true)
bool(true)
