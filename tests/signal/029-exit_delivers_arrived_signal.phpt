--TEST--
Async\signal(): a signal that arrived just before exit() completes its held Future instead of killing the process
--SKIPIF--
<?php
if (PHP_OS_FAMILY === 'Windows') echo "skip Unix-only test";
?>
--FILE--
<?php
use Async\Signal;
use function Async\delay;
use function Async\signal;

$held = signal(Signal::SIGUSR1);
$held->map(function (Signal $signal) {
    echo "delivered: ", $signal->name, "\n";
})->ignore();
delay(1);
posix_kill(getmypid(), SIGUSR1);
echo "exit\n";
exit(0);
?>
--EXPECT--
exit
delivered: SIGUSR1
