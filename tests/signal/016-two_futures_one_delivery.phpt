--TEST--
Async\signal(): one delivery completes every Future waiting for the number, and a later Future watches it again
--SKIPIF--
<?php
if (PHP_OS_FAMILY === 'Windows') echo "skip Unix-only test";
?>
--FILE--
<?php
use Async\Signal;
use function Async\await;
use function Async\delay;
use function Async\signal;
use function Async\spawn;

$first = signal(Signal::SIGUSR1);
$second = signal(Signal::SIGUSR1);

spawn(function () {
    delay(10);
    posix_kill(getmypid(), SIGUSR1);
});

echo await($first)->name, " ", await($second)->name, "\n";

$third = signal(Signal::SIGUSR1);
spawn(fn() => posix_kill(getmypid(), SIGUSR1));
echo await($third)->name, "\n";
?>
--EXPECT--
SIGUSR1 SIGUSR1
SIGUSR1
