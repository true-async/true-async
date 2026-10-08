--TEST--
Async\signal(): a watched number stays blocked whatever pcntl_sigprocmask() asks, and one the script blocked before its watch stays blocked after it
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

// SIGCHLD, unused here: the call refuses an empty list.
function is_blocked(int $signo): bool
{
    pcntl_sigprocmask(SIG_BLOCK, [SIGCHLD], $mask);
    pcntl_sigprocmask(SIG_SETMASK, $mask);

    return in_array($signo, $mask, true);
}

function poll(): void
{
    await(spawn(function () { Async\delay(10); }));
}

// Blocked before the watches, so their handles record no block of their own.
pcntl_sigprocmask(SIG_BLOCK, [SIGUSR1, SIGUSR2]);

// Keeps the registry, and its record of reblocked numbers, across the SIGUSR1 watches.
$held = signal(Signal::SIGUSR2);

// The core keeps a watched number blocked: the unblock leaves it in the mask.
$future = signal(Signal::SIGUSR1);
pcntl_sigprocmask(SIG_UNBLOCK, [SIGUSR1]);
poll();
var_dump(is_blocked(SIGUSR1));

$future->ignore();
$future = null;
var_dump(is_blocked(SIGUSR1));

// Blocked by the script before its watch: stays blocked after it.
pcntl_sigprocmask(SIG_BLOCK, [SIGUSR1]);
$future = signal(Signal::SIGUSR1);
poll();
$future->ignore();
$future = null;
var_dump(is_blocked(SIGUSR1));

$held->ignore();
$held = null;

?>
--EXPECT--
bool(true)
bool(true)
bool(true)
