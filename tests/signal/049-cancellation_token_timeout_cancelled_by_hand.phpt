--TEST--
Async\signal(): a Timeout token cancelled by hand fails the Future with the given error
--SKIPIF--
<?php
if (PHP_OS_FAMILY === 'Windows') echo "skip Unix-only test";
?>
--FILE--
<?php
use Async\Future;
use Async\Signal;
use function Async\await;
use function Async\signal;
use function Async\timeout;

function show(Future $future): void
{
    try {
        await($future);
    } catch (Throwable $e) {
        echo get_class($e), ": ", $e->getMessage(), "\n";
    }
}

// Cancelled before the wait: the 1000 ms timer cannot fire first, whatever stalls the process.
$timeout = timeout(1000);
$future = signal(Signal::SIGHUP, $timeout);
$timeout->cancel(new Async\AsyncCancellation("cancelled by hand"));
show($future);

// The Future left the watch, which unblocked SIGHUP.
pcntl_sigprocmask(SIG_UNBLOCK, [SIGWINCH], $mask);
echo "SIGHUP blocked: ", var_export(in_array(SIGHUP, $mask), true), "\n";
?>
--EXPECT--
Async\AsyncCancellation: cancelled by hand
SIGHUP blocked: false
