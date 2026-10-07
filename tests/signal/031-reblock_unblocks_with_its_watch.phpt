--TEST--
Async\signal(): the number the reactor blocked again before a poll is unblocked when its watch goes, and one the script blocks stays blocked
--EXTENSIONS--
pcntl
--SKIPIF--
<?php
if (PHP_OS_FAMILY === 'Windows') echo "skip Unix-only test";
?>
--FILE--
<?php

use Async\Signal;
use function Async\await;
use function Async\signal;
use function Async\spawn;

// Blocking SIGCHLD, which nothing here uses, to read the mask the call returns.
function is_blocked(int $signo): bool
{
    pcntl_sigprocmask(SIG_BLOCK, [SIGCHLD], $mask);

    return in_array($signo, $mask, true);
}

function poll(): void
{
    await(spawn(function () { Async\delay(10); }));
}

// Blocked first, so the handle of each watch finds it blocked and records no block of its own.
pcntl_sigprocmask(SIG_BLOCK, [SIGUSR1, SIGUSR2]);

// SIGUSR2: the script keeps it blocked through a watch held to the end, which also keeps the
// registry of watches alive across the SIGUSR1 watches below.
$held = signal(Signal::SIGUSR2);

// SIGUSR1: unblocked by the script, blocked again by the reactor's poll.
$future = signal(Signal::SIGUSR1);
pcntl_sigprocmask(SIG_UNBLOCK, [SIGUSR1]);
poll();
var_dump(is_blocked(SIGUSR1));

$future->ignore();
$future = null;
var_dump(is_blocked(SIGUSR1));

// SIGUSR1 again, blocked by the script this time: the first watch's unblock is not repeated.
pcntl_sigprocmask(SIG_BLOCK, [SIGUSR1]);
$future = signal(Signal::SIGUSR1);
poll();
$future->ignore();
$future = null;
var_dump(is_blocked(SIGUSR1));

poll();
$held->ignore();
$held = null;
var_dump(is_blocked(SIGUSR2));

?>
--EXPECT--
bool(true)
bool(false)
bool(true)
bool(true)
