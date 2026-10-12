--TEST--
Async\signal(): a signal wait the IO queue refuses makes the call throw, and the next call waits
--EXTENSIONS--
pcntl
posix
--SKIPIF--
<?php
if (PHP_OS_FAMILY === 'Windows') echo "skip Unix-only test";
?>
--FILE--
<?php
use TrueAsync\Test;

Test\fail_io('submit');

try {
    Async\signal(Async\Signal::SIGUSR1);
    echo "not reached\n";
} catch (Error $error) {
    echo get_class($error), ": ", $error->getMessage(), "\n";
}

$future = Async\signal(Async\Signal::SIGUSR1);
posix_kill(getmypid(), SIGUSR1);
var_dump(Async\await($future));
?>
--EXPECTF--
Error: Cannot submit an IO operation: %s
enum(Async\Signal::SIGUSR1)
