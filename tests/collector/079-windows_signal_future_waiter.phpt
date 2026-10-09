--TEST--
get_deadlocked_coroutines() on Windows: a coroutine awaiting an Async\signal(SIGBREAK) Future is not reported, though nothing else holds the Future
--SKIPIF--
<?php
if (PHP_OS_FAMILY !== 'Windows') echo "skip Windows-only test";
?>
--FILE--
<?php
use Async\Signal;
use function Async\signal;
use function Async\spawn;
use function Async\suspend;
use function Async\get_deadlocked_coroutines;

function start(): void
{
    spawn(function () {
        signal(Signal::SIGBREAK)->await();
    });
}

start();

for ($i = 0; $i < 4; $i++) {
    suspend();
}

echo count(get_deadlocked_coroutines()), " found\n";
exit(0);
?>
--EXPECT--
0 found
