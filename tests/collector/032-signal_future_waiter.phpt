--TEST--
get_deadlocked_coroutines(): a coroutine awaiting the Future of Async\signal() that nothing else holds is not reported
--SKIPIF--
<?php
if (PHP_OS_FAMILY === 'Windows') die('skip POSIX signals');
if (!function_exists('posix_kill')) die('skip posix needed');
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
        signal(Signal::SIGUSR1)->await();
        echo "signal received\n";
    });
}

start();

for ($i = 0; $i < 4; $i++) {
    suspend();
}

echo count(get_deadlocked_coroutines()), " found\n";
posix_kill(getmypid(), SIGUSR1);
Async\delay(20);
echo "end\n";
?>
--EXPECT--
0 found
signal received
end
