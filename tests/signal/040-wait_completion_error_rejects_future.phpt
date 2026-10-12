--TEST--
Async\signal(): a signal wait that the IO queue completes with an error rejects the Future with that error
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

// SIGWINCH is ignored by default: where the queue leaves the signal pending, its release raises it harmlessly.
$future = Async\signal(Async\Signal::SIGWINCH);
Test\fail_io('complete');
posix_kill(getmypid(), SIGWINCH);

try {
    Async\await($future);
    echo "not reached\n";
} catch (Error $error) {
    echo get_class($error), ": ", $error->getMessage(), "\n";
}
?>
--EXPECTF--
Error: The signal wait ended with status %d: %s
