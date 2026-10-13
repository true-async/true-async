--TEST--
FileSystemEvent: new FileSystemEvent() leaves its readonly properties uninitialised
--FILE--
<?php

use Async\FileSystemWatcher;
$event = new Async\FileSystemEvent();

try {
    echo $event->path;
} catch (Error $error) {
    echo $error->getMessage(), "\n";
}
?>
--EXPECT--
Typed property Async\FileSystemEvent::$path must not be accessed before initialization
