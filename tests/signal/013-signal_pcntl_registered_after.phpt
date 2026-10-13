--TEST--
Async\signal() keeps receiving after pcntl_signal() re-registers the same signal
--EXTENSIONS--
pcntl
posix
--SKIPIF--
<?php
if (PHP_OS_FAMILY === 'Windows') echo "skip Unix-only test";
?>
--FILE--
<?php
// pcntl_signal() after Async\signal() goes through zend_signal() and
// replaces the process sigaction, detaching libuv — Symfony Console does
// exactly this inside every artisan command (laravel-spawn#8). The
// reactor must re-arm its handler before blocking, so both the async
// waiter and the pcntl callback fire from a single delivery.

use Async\Signal;
use function Async\await;
use function Async\delay;
use function Async\signal;
use function Async\spawn;
use function Async\suspend;

$watching = false;
$received = false;

spawn(function () use (&$watching, &$received) {
    $signal = signal(Signal::SIGUSR1);
    $watching = true;
    $sig = await($signal);
    echo "async: {$sig->name}\n";
    $received = true;
});

spawn(function () use (&$watching, &$received) {
    while (!$watching) {
        suspend();
    }
    pcntl_signal(SIGUSR1, function () {
        echo "pcntl\n";
    });

    delay(200);
    posix_kill(posix_getpid(), SIGUSR1);
    while (!$received) {
        delay(1);
    }
    pcntl_signal_dispatch();
    echo "end\n";
});
?>
--EXPECT--
async: SIGUSR1
pcntl
end
