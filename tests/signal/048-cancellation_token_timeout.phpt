--TEST--
Async\signal(): a timeout token firing while the Future waits fails it with the TimeoutException
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

$future = signal(Signal::SIGHUP, timeout(10));
pcntl_sigprocmask(SIG_UNBLOCK, [SIGWINCH], $mask);
echo "SIGHUP blocked: ", var_export(in_array(SIGHUP, $mask), true), "\n";
show($future);

// The Future left the watch, which unblocked SIGHUP.
pcntl_sigprocmask(SIG_UNBLOCK, [SIGWINCH], $mask);
echo "SIGHUP blocked: ", var_export(in_array(SIGHUP, $mask), true), "\n";
?>
--EXPECT--
SIGHUP blocked: true
Async\TimeoutException: Timeout occurred after 10 milliseconds
SIGHUP blocked: false
