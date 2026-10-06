--TEST--
Async\signal() outside the CLI in a thread-safe build throws the core's error: the signal mask is per thread
--CGI--
--SKIPIF--
<?php
if (PHP_OS_FAMILY === 'Windows') echo "skip Unix-only test";
if (!PHP_ZTS) echo "skip thread-safe builds only";
?>
--FILE--
<?php
try {
    Async\signal(Async\Signal::SIGUSR1);
} catch (Throwable $e) {
    echo get_class($e), ": ", $e->getMessage(), "\n";
}
?>
--EXPECT--
Io\Poll\PollException: Io\Poll\SignalHandle is only available in the CLI in thread-safe builds
