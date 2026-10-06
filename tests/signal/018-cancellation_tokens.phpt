--TEST--
Async\signal(): a token completing while the Future waits fails it with the token's error, or AsyncCancellation without one
--SKIPIF--
<?php
if (PHP_OS_FAMILY === 'Windows') echo "skip Unix-only test";
?>
--FILE--
<?php
use Async\FutureState;
use Async\Future;
use Async\Signal;
use function Async\await;
use function Async\delay;
use function Async\signal;
use function Async\spawn;
use function Async\timeout;

function show(Future $future): void
{
    try {
        await($future);
    } catch (Throwable $e) {
        echo get_class($e), ": ", $e->getMessage(), "\n";
    }
}

$state = new FutureState();
$future = signal(Signal::SIGHUP, new Future($state));
spawn(function () use ($state) {
    delay(10);
    $state->error(new RuntimeException("state failed"));
});
show($future);

$state = new FutureState();
$future = signal(Signal::SIGHUP, new Future($state));
spawn(function () use ($state) {
    delay(10);
    $state->complete(1);
});
show($future);

$future = signal(Signal::SIGHUP, spawn(fn() => delay(10)));
show($future);

$future = signal(Signal::SIGHUP, timeout(10));
pcntl_sigprocmask(SIG_UNBLOCK, [SIGWINCH], $mask);
echo "SIGHUP blocked: ", var_export(in_array(SIGHUP, $mask), true), "\n";
show($future);

$timeout = timeout(1000);
$future = signal(Signal::SIGHUP, $timeout);
spawn(fn() => $timeout->cancel(new Async\AsyncCancellation("cancelled by hand")));
show($future);

// Each Future left the watch, which unblocked SIGHUP.
pcntl_sigprocmask(SIG_UNBLOCK, [SIGWINCH], $mask);
echo "SIGHUP blocked: ", var_export(in_array(SIGHUP, $mask), true), "\n";
?>
--EXPECT--
RuntimeException: state failed
Async\AsyncCancellation: Signal wait cancelled
Async\AsyncCancellation: Signal wait cancelled
SIGHUP blocked: true
Async\TimeoutException: Timeout occurred after 10 milliseconds
Async\AsyncCancellation: cancelled by hand
SIGHUP blocked: false
