--TEST--
Async\signal() on the Poll queue: the reactor takes the signal from its source, a pcntl handler gets it too, and the number is unblocked after the last Future
--SKIPIF--
<?php
if (PHP_OS_FAMILY !== 'Linux') echo "skip Linux-only test (/proc)";
?>
--FILE--
<?php
use Async\Signal;
use function Async\await;
use function Async\delay;
use function Async\signal;
use function Async\spawn;

TrueAsync\Test\reactor_use_poll_queue();

function blocked(int $signo): bool
{
    preg_match('/^SigBlk:\s*(\S+)/m', file_get_contents('/proc/thread-self/status'), $match);

    return (hexdec($match[1]) >> ($signo - 1) & 1) === 1;
}

pcntl_signal(SIGUSR2, function () {
    echo "pcntl\n";
});

$first = signal(Signal::SIGUSR2);
$second = signal(Signal::SIGUSR2);
echo "blocked while waiting: ", var_export(blocked(SIGUSR2), true), "\n";

spawn(function () {
    delay(10);
    posix_kill(getmypid(), SIGUSR2);
});

echo await($first)->name, " ", await($second)->name, "\n";
pcntl_signal_dispatch();
echo "blocked after: ", var_export(blocked(SIGUSR2), true), "\n";
?>
--EXPECT--
blocked while waiting: true
SIGUSR2 SIGUSR2
pcntl
blocked after: false
