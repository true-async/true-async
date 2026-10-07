--TEST--
Async\signal(): a watch that goes inside a pcntl handler leaves its number unblocked after the dispatch
--EXTENSIONS--
pcntl
posix
--SKIPIF--
<?php
if (PHP_OS_FAMILY === 'Windows') echo "skip Unix-only test";
?>
--FILE--
<?php

use Async\Signal;
use function Async\signal;

// Blocking SIGCHLD, which nothing here uses, to read the mask the call returns.
function is_blocked(int $signo): bool
{
    pcntl_sigprocmask(SIG_BLOCK, [SIGCHLD], $mask);

    return in_array($signo, $mask, true);
}

$future = signal(Signal::SIGHUP);
var_dump(is_blocked(SIGHUP));

pcntl_signal(SIGUSR1, function () use (&$future) {
    $future->ignore();
    $future = null;
});

posix_kill(posix_getpid(), SIGUSR1);
pcntl_signal_dispatch();

var_dump(is_blocked(SIGHUP));

?>
--EXPECT--
bool(true)
bool(false)
