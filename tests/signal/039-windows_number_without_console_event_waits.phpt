--TEST--
Async\signal() on Windows: the Future of SIGUSR1, which no console event delivers, completes only through its cancellation
--SKIPIF--
<?php
if (PHP_OS_FAMILY !== 'Windows') echo "skip Windows-only test";
?>
--FILE--
<?php
use Async\Signal;
use Async\TimeoutException;
use function Async\await;
use function Async\signal;
use function Async\timeout;

try {
    await(signal(Signal::SIGUSR1, timeout(50)));
} catch (TimeoutException $e) {
    echo "timeout\n";
}
?>
--EXPECT--
timeout
