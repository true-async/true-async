--TEST--
FileSystemWatcher: an extension with a dot inside throws a ValueError
--FILE--
<?php

use Async\FileSystemWatcher;
try {
    new FileSystemWatcher(sys_get_temp_dir(), extensions: ['tar.gz']);
} catch (ValueError $error) {
    echo $error->getMessage(), "\n";
}
?>
--EXPECT--
Async\FileSystemWatcher::__construct(): Argument #6 ($extensions) must not contain an extension with a dot or a slash inside
