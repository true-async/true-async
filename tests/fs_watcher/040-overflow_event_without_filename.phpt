--TEST--
FileSystemWatcher: an overflow of the kernel's queue gives one event with no filename and both flags
--FILE--
<?php

use Async\FileSystemWatcher;
use TrueAsync\Test;

$dir = sys_get_temp_dir() . '/fsw_040_' . getmypid();
mkdir($dir);
$watcher = new FileSystemWatcher($dir, extensions: ['php']);

touch("$dir/a.php");
Test\fail_io('inotify_overflow');

foreach ($watcher as $event) {
    echo var_export($event->filename, true), ' renamed=', (int) $event->renamed, ' changed=', (int) $event->changed, "\n";

    if ($event->filename === 'a.php') {
        break;
    }
}

$watcher->close();
unlink("$dir/a.php");
rmdir($dir);
?>
--EXPECT--
NULL renamed=1 changed=1
'a.php' renamed=1 changed=1
