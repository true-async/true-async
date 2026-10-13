--TEST--
Async\signal(): a Coroutine token finishing while the Future waits fails it with AsyncCancellation
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
use function Async\spawn;

function show(Future $future): void
{
    try {
        await($future);
    } catch (Throwable $e) {
        echo get_class($e), ": ", $e->getMessage(), "\n";
    }
}

$future = signal(Signal::SIGHUP, spawn(fn() => null));
show($future);

// The Future left the watch, which unblocked SIGHUP.
pcntl_sigprocmask(SIG_UNBLOCK, [SIGWINCH], $mask);
echo "SIGHUP blocked: ", var_export(in_array(SIGHUP, $mask), true), "\n";
?>
--EXPECT--
Async\AsyncCancellation: Signal wait cancelled
SIGHUP blocked: false
