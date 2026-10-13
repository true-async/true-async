--TEST--
FileSystemWatcher: an empty extension, or a bare dot, throws a ValueError
--FILE--
<?php

use Async\FileSystemWatcher;
try {
    new FileSystemWatcher(sys_get_temp_dir(), extensions: ['.']);
} catch (ValueError $error) {
    echo $error->getMessage(), "\n";
}
?>
--EXPECT--
Async\FileSystemWatcher::__construct(): Argument #6 ($extensions) must not contain an empty extension
