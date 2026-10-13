--TEST--
FileSystemWatcher: a recursive watch of a deep tree made in a coroutine with a small stack names the deepest change
--SKIPIF--
<?php
if (PHP_OS_FAMILY === 'Windows') echo "skip the path is longer than Windows' MAX_PATH";
if (getenv('ASAN_OPTIONS') !== false) echo "skip a sanitizer build reserves more than a 128K stack";
?>
--INI--
fiber.stack_size=128K
--FILE--
<?php

use Async\FileSystemWatcher;
use function Async\await;
use function Async\spawn;

$dir = sys_get_temp_dir() . '/fsw_071_' . getmypid();
$depth = 1000;
$deep = $dir . str_repeat('/d', $depth);
mkdir($deep, 0777, true);

// The walk runs on the coroutine's stack; the touch, whose realpath recurses per level, on main's.
$watcher = await(spawn(fn () => new FileSystemWatcher($dir, recursive: true)));
touch("$deep/x");

await(spawn(function () use ($watcher, $depth) {
    foreach ($watcher as $event) {
        var_dump($event->filename === str_repeat('d/', $depth) . 'x');
        break;
    }
}));

$watcher->close();
unlink("$deep/x");

for ($path = $deep; $path !== $dir; $path = dirname($path)) {
    rmdir($path);
}

rmdir($dir);
?>
--EXPECT--
bool(true)
