--TEST--
FileSystemWatcher: a negative $debounceMs throws a ValueError
--FILE--
<?php

use Async\FileSystemWatcher;
try {
    new FileSystemWatcher(sys_get_temp_dir(), debounceMs: -1);
} catch (ValueError $error) {
    echo $error->getMessage(), "\n";
}
?>
--EXPECT--
Async\FileSystemWatcher::__construct(): Argument #4 ($debounceMs) must be between 0 and 4294967295
