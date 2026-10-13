--TEST--
FileSystemWatcher: a monitoring error no foreach threw is freed with the watcher
--FILE--
<?php

use Async\FileSystemWatcher;
use TrueAsync\Test;
use function Async\suspend;

$dir = sys_get_temp_dir() . '/fsw_057_' . getmypid();
mkdir($dir);
$watcher = new FileSystemWatcher($dir);

touch("$dir/a");
Test\fail_io('complete');

while (!$watcher->isClosed()) {
    suspend();
}

unset($watcher);
echo "freed\n";
unlink("$dir/a");
rmdir($dir);
?>
--EXPECT--
freed
