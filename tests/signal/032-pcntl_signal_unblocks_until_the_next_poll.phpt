--TEST--
Async\signal(): a delivery between pcntl_signal() and the next poll goes to the pcntl handler alone
--EXTENSIONS--
pcntl
posix
--SKIPIF--
<?php
if (PHP_OS_FAMILY === 'Windows') echo "skip Unix-only test";
?>
--FILE--
<?php

// pcntl_signal() goes through zend_sigaction(), which unblocks the number it installs a handler for
// (Zend/zend_signal.c), and the watch sees a signal only while it is blocked. The reactor blocks the
// watched numbers again before its next poll, so a delivery in between reaches the handler and not
// the Future. The core change of dev/RFC-CHANGES.md 5 makes the Future take this delivery too, and
// this expectation changes with it.

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

// Nothing suspends between the unblock and this delivery.
posix_kill(posix_getpid(), SIGUSR1);
pcntl_signal_dispatch();
echo "pending\n";

spawn(function () {
    delay(100);
    posix_kill(posix_getpid(), SIGUSR1);
});

?>
--EXPECT--
handler
pending
future: SIGUSR1
