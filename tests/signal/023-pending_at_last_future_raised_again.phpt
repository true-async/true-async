--TEST--
Async\signal(): a signal still pending when the last Future goes is raised again, so a pcntl handler gets it
--SKIPIF--
<?php
if (PHP_OS_FAMILY === 'Windows') echo "skip Unix-only test";
?>
--FILE--
<?php
use Async\Signal;
use function Async\signal;

pcntl_signal(SIGUSR1, function () {
    echo "pcntl\n";
});

$future = signal(Signal::SIGUSR1);
$future->ignore();
posix_kill(getmypid(), SIGUSR1);

// No suspension: the reactor never took the signal, so it is pending in the watch's handle.
unset($future);
pcntl_signal_dispatch();
echo "end\n";
?>
--EXPECT--
pcntl
end
