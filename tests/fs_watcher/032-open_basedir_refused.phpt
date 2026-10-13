--TEST--
FileSystemWatcher: a path outside open_basedir is refused
--FILE--
<?php

use Async\FileSystemWatcher;
// At run time: an --INI-- line with Windows' short temp path (RUNNER~1) does not parse.
ini_set('open_basedir', sys_get_temp_dir());

try {
    new FileSystemWatcher('/');
} catch (Async\AsyncException $exception) {
    echo $exception->getMessage(), "\n";
}
?>
--EXPECTF--
Warning: Async\FileSystemWatcher::__construct(): open_basedir restriction in effect. File(/) is not within the allowed path(s): (%s) in %s on line %d
Failed to start filesystem handle: open_basedir restriction in effect
