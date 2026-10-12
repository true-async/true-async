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
        $signal = signal(Signal::SIGUSR1);
        $GLOBALS['watching'] = true;
        $signal->await();
        echo "signal received\n";
        $GLOBALS['received'] = true;
    });
}

$watching = false;
$received = false;
start();

// The coroutine parks right after the flag; under a seed one suspend() need not run it, and a signal sent
// before the watch would end the process
while (!$watching) {
    suspend();
}

echo count(get_deadlocked_coroutines()), " found\n";
posix_kill(getmypid(), SIGUSR1);

// Main holds nothing of the coroutine, so it waits on a flag; one delay could end before the signal
while (!$received) {
    Async\delay(1);
}

echo "end\n";
?>
--EXPECT--
0 found
signal received
end
