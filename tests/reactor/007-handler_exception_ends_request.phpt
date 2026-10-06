--TEST--
An exception a pcntl handler throws while every coroutine waits ends the request, as one a microtask throws: the waiters are cancelled
--EXTENSIONS--
pcntl
--FILE--
<?php
use TrueAsync\Test;

pcntl_async_signals(true);
pcntl_signal(SIGUSR1, function () {
    throw new RuntimeException("from the handler");
});

try {
    Test\reactor_wait(60000, SIGUSR1);
} catch (Async\AsyncCancellation $e) {
    echo "main: ", $e->getMessage(), "\n";
}
?>
--EXPECTF--
main: Graceful shutdown

Fatal error: Uncaught RuntimeException: from the handler in %s:%d
Stack trace:
#0 [internal function]: {closure:%s}(%d, Array)
#1 {main}
  thrown in %s on line %d
