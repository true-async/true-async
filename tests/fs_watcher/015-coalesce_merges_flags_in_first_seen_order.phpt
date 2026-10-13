--TEST--
FileSystemWatcher: coalesce mode gives one event per entry in first-seen order, its flags merged
--FILE--
<?php

use Async\FileSystemWatcher;
$dir = sys_get_temp_dir() . '/fsw_015_' . getmypid();
mkdir($dir);
$watcher = new FileSystemWatcher($dir);

file_put_contents("$dir/a", '1');
file_put_contents("$dir/b", '1');
file_put_contents("$dir/a", '2');
rename("$dir/b", "$dir/c");

foreach ($watcher as $event) {
    echo $event->filename, ' renamed=', (int) $event->renamed, ' changed=', (int) $event->changed, "\n";

    if ($event->filename === 'c') {
        break;
    }
}

$watcher->close();
unlink("$dir/a");
unlink("$dir/c");
rmdir($dir);
?>
--EXPECT--
a renamed=1 changed=1
b renamed=1 changed=1
c renamed=1 changed=0
