--TEST--
Async\signal(): exit() with a dropped map() chain on a signal Future that never arrives frees the chain at the request's end
--SKIPIF--
<?php
if (PHP_OS_FAMILY === 'Windows') echo "skip Unix-only test";
?>
--FILE--
<?php
use Async\Signal;
use function Async\signal;

signal(Signal::SIGUSR1)->map(function (Signal $signal) {
    echo "delivered: ", $signal->name, "\n";
})->ignore();
echo "exit\n";
exit(0);
?>
--EXPECT--
exit
