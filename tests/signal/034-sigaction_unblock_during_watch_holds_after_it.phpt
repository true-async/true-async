--TEST--
Async\signal(): a number zend_sigaction() unblocked during the watch is unblocked after it
--EXTENSIONS--
pcntl
--SKIPIF--
<?php
if (PHP_OS_FAMILY === 'Windows') echo "skip Unix-only test";
if (!function_exists('TrueAsync\Test\zend_sigaction_reinstall')) echo "skip Zend signals are disabled";
?>
--FILE--
<?php

use Async\Signal;
use function Async\await;
use function Async\delay;
use function Async\signal;
use function Async\spawn;

// SIGCHLD, unused here: the call refuses an empty list. SIG_SETMASK on purpose: a save-and-restore
// during the watch must not take back the unblock zend_sigaction() did.
function is_blocked(int $signo): bool
{
    pcntl_sigprocmask(SIG_BLOCK, [SIGCHLD], $mask);
    pcntl_sigprocmask(SIG_SETMASK, $mask);

    return in_array($signo, $mask, true);
}

function poll(): void
{
    await(spawn(function () { delay(10); }));
}

// Blocked by the script before the watch, unblocked behind pcntl's back during it.
pcntl_sigprocmask(SIG_BLOCK, [SIGUSR1]);
$future = signal(Signal::SIGUSR1);
TrueAsync\Test\zend_sigaction_reinstall(SIGUSR1);
// The reactor's poll blocks it again and leaves the unblock for the end of the watch.
poll();
var_dump(is_blocked(SIGUSR1));

$future->ignore();
$future = null;
var_dump(is_blocked(SIGUSR1));

?>
--EXPECT--
bool(true)
bool(false)
