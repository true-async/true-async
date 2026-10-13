--TEST--
FileSystemWatcher: a non-string extension throws a TypeError
--FILE--
<?php

use Async\FileSystemWatcher;
try {
    new FileSystemWatcher(sys_get_temp_dir(), extensions: ['php', 1]);
} catch (TypeError $error) {
    echo $error->getMessage(), "\n";
}
?>
--EXPECT--
Async\FileSystemWatcher::__construct(): Argument #6 ($extensions) must contain only strings, int given
