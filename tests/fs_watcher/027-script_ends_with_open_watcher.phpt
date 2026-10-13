--TEST--
FileSystemWatcher: the script ends after its last line while a watcher is open and nobody iterates
--FILE--
<?php

use Async\FileSystemWatcher;
$dir = sys_get_temp_dir() . '/fsw_027_' . getmypid();
mkdir($dir);
register_shutdown_function(fn() => rmdir($dir));
$watcher = new FileSystemWatcher($dir);
Async\spawn(function () {
    echo "child ran\n";
});
echo "main end\n";
?>
--EXPECT--
main end
child ran
