--TEST--
FileSystemWatcher: a $maxHoldMs above 2^32-1 throws a ValueError
--FILE--
<?php

use Async\FileSystemWatcher;
try {
    new FileSystemWatcher(sys_get_temp_dir(), maxHoldMs: 4294967296);
} catch (ValueError $error) {
    echo $error->getMessage(), "\n";
}
?>
--EXPECT--
Async\FileSystemWatcher::__construct(): Argument #5 ($maxHoldMs) must be between 0 and 4294967295
