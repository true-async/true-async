--TEST--
FileSystemWatcher: a recursive watch names an entry two levels down in a tree that existed before it by its path from the root
--FILE--
<?php

use Async\FileSystemWatcher;

$dir = sys_get_temp_dir() . '/fsw_064_' . getmypid();
mkdir("$dir/src/lib", 0777, true);
$watcher = new FileSystemWatcher($dir, recursive: true, extensions: ['php']);

file_put_contents("$dir/src/lib/util.php", '<?php');

foreach ($watcher as $event) {
    // Windows names the entry with backslashes and also reports the directories' times; the filter drops those.
    echo str_replace('\\', '/', $event->filename), "\n";
    break;
}

$watcher->close();
unlink("$dir/src/lib/util.php");
rmdir("$dir/src/lib");
rmdir("$dir/src");
rmdir($dir);
?>
--EXPECT--
src/lib/util.php
