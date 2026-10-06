--TEST--
Async\signal(): a number the core cannot block and a number the platform lacks throw, and leave nothing watched
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

foreach ([Signal::SIGKILL, Signal::SIGSEGV, Signal::SIGBREAK] as $signal) {
    try {
        signal($signal);
    } catch (ValueError $e) {
        echo $signal->name, ": ", $e->getMessage(), "\n";
    }
}

$future = signal(Signal::SIGUSR1);
spawn(fn() => posix_kill(getmypid(), SIGUSR1));
echo await($future)->name, "\n";
?>
--EXPECTF--
SIGKILL: Io\Poll\SignalHandle::__construct(): Argument #1 ($signals) must not contain signal %d, which cannot be blocked
SIGSEGV: Io\Poll\SignalHandle::__construct(): Argument #1 ($signals) must not contain signal %d, which cannot be blocked
SIGBREAK: Async\signal(): Argument #1 ($signal) must not be SIGBREAK, which this platform lacks
SIGUSR1
