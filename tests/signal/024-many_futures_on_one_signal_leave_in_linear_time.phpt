--TEST--
Async\signal(): a thousand Futures on one signal, dropped in the order they were made, leave its watch each at its slot, with no search
--DESCRIPTION--
A search per Future made the drop quadratic in the Futures (DECISIONS 2026-10-07); the count of searches stands for the time.
--SKIPIF--
<?php
if (PHP_OS_FAMILY === 'Windows') echo "skip Unix-only test";
?>
--FILE--
<?php
use Async\Signal;
use TrueAsync\Test;
use function Async\signal;

$futures = [];

for ($i = 0; $i < 1000; $i++) {
    $futures[] = signal(Signal::SIGUSR1)->ignore();
}

$searches = Test\callback_searches();
$futures = null;
echo "searches: ", Test\callback_searches() - $searches, "\n";
?>
--EXPECT--
searches: 0
