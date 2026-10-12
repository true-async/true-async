--TEST--
Async\signal(): a dropped map() chain on a waiting signal Future survives a collection and runs on the delivery
--SKIPIF--
<?php
if (PHP_OS_FAMILY === 'Windows') echo "skip Unix-only test";
if (!function_exists('posix_kill')) echo "skip posix extension required";
?>
--FILE--
<?php
use Async\Future;
use Async\FutureState;
use Async\Signal;
use function Async\signal;

$delivered = new FutureState();
signal(Signal::SIGUSR1)->map(function (Signal $signal) use ($delivered) {
    echo "delivered: ", $signal->name, "\n";
    $delivered->complete(null);
})->ignore();

var_dump(gc_collect_cycles());
posix_kill(getmypid(), SIGUSR1);
(new Future($delivered))->await();
?>
--EXPECT--
int(0)
delivered: SIGUSR1
