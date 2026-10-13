--TEST--
FileSystemWatcher: a path with a NUL byte throws a ValueError
--FILE--
<?php

use Async\FileSystemWatcher;
try {
    new FileSystemWatcher(sys_get_temp_dir() . "\0x");
} catch (ValueError $error) {
    echo $error->getMessage(), "\n";
}
?>
--EXPECT--
Async\FileSystemWatcher::__construct(): Argument #1 ($path) must not contain any null bytes
