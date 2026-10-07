--TEST--
Async\signal(): a script that ends by itself waits for the signal of a Future it holds
--SKIPIF--
<?php
if (PHP_OS_FAMILY === 'Windows') echo "skip Unix-only test";
?>
--FILE--
<?php
use Async\Signal;
use function Async\signal;

$held = signal(Signal::SIGUSR1);
$held->map(function (Signal $signal) {
    echo "delivered: ", $signal->name, "\n";
})->ignore();

exec('(sleep 0.2; kill -USR1 ' . getmypid() . ') > /dev/null 2>&1 &');
echo "end\n";
?>
--EXPECT--
end
delivered: SIGUSR1
