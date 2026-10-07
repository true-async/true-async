--TEST--
Async\signal(): a delivery between pcntl_signal() and the next poll misses the Future (known, dev/RFC-CHANGES.md 5)
--EXTENSIONS--
pcntl
posix
--SKIPIF--
<?php
if (PHP_OS_FAMILY === 'Windows') echo "skip Unix-only test";
?>
--FILE--
<?php

// zend_sigaction() in pcntl_signal() unblocks SIGUSR1 until the next poll blocks it again. With the
// core hook of dev/RFC-CHANGES.md 5 the Future gets this delivery.

use Async\Signal;
use function Async\await;
use function Async\delay;
use function Async\signal;
use function Async\spawn;

$future = signal(Signal::SIGUSR1);

spawn(function () use ($future) {
    echo "future: ", await($future)->name, "\n";
});

pcntl_signal(SIGUSR1, function () {
    echo "handler\n";
});

posix_kill(posix_getpid(), SIGUSR1);
pcntl_signal_dispatch();
echo "pending\n";

spawn(function () {
    delay(1);
    posix_kill(posix_getpid(), SIGUSR1);
});

?>
--EXPECT--
handler
pending
future: SIGUSR1
