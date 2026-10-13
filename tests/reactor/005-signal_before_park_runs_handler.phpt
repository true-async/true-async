--TEST--
A signal that lands before the park, while every coroutine then waits, runs its pcntl handler in a coroutine, which may spawn
--EXTENSIONS--
pcntl
--FILE--
<?php
use function Async\spawn;
use function Async\await;
use TrueAsync\Test;

$main = Async\current_coroutine();
$waiter = null;
pcntl_async_signals(true);
pcntl_signal(SIGUSR1, function () use ($main, &$waiter) {
    $current = Async\current_coroutine();
    echo "handler, in a coroutine other than main: ", var_export($current !== null && $current !== $main, true), "\n";
    spawn(function () use ($waiter) {
        echo "spawned by the handler\n";
        $waiter->cancel();
    });
});

// The handler's coroutine ends the wait, not a deadline: a timer due first would wake the waiter
// before the handler runs.
$waiter = spawn(function () {
    try {
        Test\reactor_wait(100000, SIGUSR1);
    } catch (Async\AsyncCancellation) {
        echo "woken\n";
    }
});
await($waiter);
?>
--EXPECT--
handler, in a coroutine other than main: true
spawned by the handler
woken
