--TEST--
Async\signal(): a million Futures on one signal, dropped in the order they were made, leave its watch well under the time limit
--SKIPIF--
<?php
if (PHP_OS_FAMILY === 'Windows') echo "skip Unix-only test";
?>
--INI--
max_execution_time=20
memory_limit=1G
--FILE--
<?php
use Async\Signal;
use function Async\signal;

$futures = [];

for ($i = 0; $i < 1000000; $i++) {
    $futures[] = signal(Signal::SIGUSR1)->ignore();
}

$futures = null;
echo "end\n";
?>
--EXPECT--
end
