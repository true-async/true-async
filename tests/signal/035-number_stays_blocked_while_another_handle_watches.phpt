--TEST--
Async\signal(): the end of a watch leaves the number blocked while the script's own SignalHandle still watches it
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

// SIGCHLD, unused here: the call refuses an empty list.
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

$context = new Io\Poll\Context();
$watcher = $context->add(new Io\Poll\SignalHandle([SIGUSR1]), [Io\Poll\Event::Signal]);

$future = signal(Signal::SIGUSR1);
TrueAsync\Test\zend_sigaction_reinstall(SIGUSR1);
// The reactor's poll blocks it again.
poll();
$future->ignore();
$future = null;
var_dump(is_blocked(SIGUSR1));

$watcher->remove();
var_dump(is_blocked(SIGUSR1));

?>
--EXPECT--
bool(true)
bool(false)
