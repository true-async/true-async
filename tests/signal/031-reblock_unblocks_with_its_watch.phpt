--TEST--
Async\signal(): a watched number stays blocked whatever pcntl_sigprocmask() asks; after the watch it is unblocked if the script unblocked it during the watch, and stays blocked otherwise
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

// SIGCHLD, unused here: the call refuses an empty list. No SIG_SETMASK: during a watch a mask that
// holds the number would take back an unblock the script asked for.
function is_blocked(int $signo): bool
{
    pcntl_sigprocmask(SIG_BLOCK, [SIGCHLD], $mask);
    if (!in_array(SIGCHLD, $mask, true)) {
        pcntl_sigprocmask(SIG_UNBLOCK, [SIGCHLD]);
    }

    return in_array($signo, $mask, true);
}

function poll(): void
{
    await(spawn(function () { Async\delay(10); }));
}

// Blocked before the watches, so their handles record no block of their own.
pcntl_sigprocmask(SIG_BLOCK, [SIGUSR1, SIGUSR2]);

// Keeps the registry alive across the SIGUSR1 watches, so they share its Context.
$held = signal(Signal::SIGUSR2);

// The core keeps a watched number blocked: the unblock leaves it in the mask.
$future = signal(Signal::SIGUSR1);
pcntl_sigprocmask(SIG_UNBLOCK, [SIGUSR1]);
poll();
var_dump(is_blocked(SIGUSR1));

// The unblock the script asked for waits for the end of the watch.
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
bool(false)
bool(true)
