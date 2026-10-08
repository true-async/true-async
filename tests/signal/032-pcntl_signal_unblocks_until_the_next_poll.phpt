--TEST--
Async\signal(): a delivery right after pcntl_signal() goes to the Future, not to the handler
--EXTENSIONS--
pcntl
posix
--SKIPIF--
<?php
if (PHP_OS_FAMILY === 'Windows') echo "skip Unix-only test";
?>
--FILE--
<?php

// zend_sigaction() in pcntl_signal() unblocks SIGUSR1; the core blocks a watched number again at
// once, so the delivery waits for the next poll (dev/RFC-CHANGES.md 5).

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
pending
future: SIGUSR1
