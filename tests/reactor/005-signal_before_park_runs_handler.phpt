--TEST--
A signal that lands before the park, while every coroutine then waits, runs its pcntl handler in a coroutine, which may spawn
--EXTENSIONS--
pcntl
--FILE--
<?php
use function Async\spawn;
use TrueAsync\Test;

$main = Async\current_coroutine();
pcntl_async_signals(true);
pcntl_signal(SIGUSR1, function () use ($main) {
    $current = Async\current_coroutine();
    echo "handler, in a coroutine other than main: ", var_export($current !== null && $current !== $main, true), "\n";
    spawn(fn() => print("spawned by the handler\n"));
});

Test\reactor_wait(100, SIGUSR1);
echo "woken\n";
?>
--EXPECT--
handler, in a coroutine other than main: true
spawned by the handler
woken
