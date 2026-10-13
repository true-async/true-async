--TEST--
FileSystemWatcher: a path outside open_basedir is refused
--INI--
open_basedir={TMP}
--FILE--
<?php

use Async\FileSystemWatcher;
try {
    new FileSystemWatcher('/');
} catch (Async\AsyncException $exception) {
    echo $exception->getMessage(), "\n";
}
?>
--EXPECTF--
Warning: Async\FileSystemWatcher::__construct(): open_basedir restriction in effect. File(/) is not within the allowed path(s): (%s) in %s on line %d
Failed to start filesystem handle: open_basedir restriction in effect
