--TEST--
Async\signal() forwards a valid siginfo to a pre-registered pcntl handler (no SEGV)
--EXTENSIONS--
pcntl
posix
--SKIPIF--
<?php
if (PHP_OS_FAMILY === 'Windows') echo "skip Unix-only test";
?>
--FILE--
<?php
// pcntl_signal() first, Async\signal() second: uv_signal_start() takes
// over the sigaction and the uv callback forwards the Zend handler
// chain. pcntl's SA_SIGINFO handler copies *siginfo — forwarding with
// siginfo=NULL crashed the process on the first delivery.

use Async\Signal;
use function Async\await;
use function Async\delay;
use function Async\signal;
use function Async\spawn;
use function Async\suspend;

pcntl_signal(SIGUSR2, function () {
    echo "pcntl\n";
});

$watching = false;
$received = false;

spawn(function () use (&$watching, &$received) {
    $signal = signal(Signal::SIGUSR2);
    $watching = true;
    $sig = await($signal);
    echo "async: {$sig->name}\n";
    $received = true;
});

spawn(function () use (&$watching, &$received) {
    while (!$watching) {
        suspend();
    }
    posix_kill(posix_getpid(), SIGUSR2);
    while (!$received) {
        delay(1);
    }
    pcntl_signal_dispatch();
    echo "end\n";
});
?>
--EXPECT--
async: SIGUSR2
pcntl
end
