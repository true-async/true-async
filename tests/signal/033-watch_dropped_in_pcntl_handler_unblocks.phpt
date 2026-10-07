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

// SIGCHLD, unused here: the call refuses an empty list.
function is_blocked(int $signo): bool
{
    pcntl_sigprocmask(SIG_BLOCK, [SIGCHLD], $mask);
    pcntl_sigprocmask(SIG_SETMASK, $mask);

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
