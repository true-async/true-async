--TEST--
FileSystemWatcher: raw mode gives every kernel event in order
--FILE--
<?php

use Async\FileSystemWatcher;
$dir = sys_get_temp_dir() . '/fsw_016_' . getmypid();
mkdir($dir);
$watcher = new FileSystemWatcher($dir, coalesce: false);

file_put_contents("$dir/a", '1');
rename("$dir/a", "$dir/b");

foreach ($watcher as $event) {
    echo $event->filename, ' renamed=', (int) $event->renamed, ' changed=', (int) $event->changed, "\n";

    if ($event->filename === 'b') {
        break;
    }
}

$watcher->close();
unlink("$dir/b");
rmdir($dir);
?>
--EXPECT--
a renamed=1 changed=0
a renamed=0 changed=1
a renamed=1 changed=0
b renamed=1 changed=0
