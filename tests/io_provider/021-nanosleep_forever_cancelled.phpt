--TEST--
time_nanosleep() past the clock's range parks on the latest finite Timer and ends when its coroutine is cancelled
--FILE--
<?php
use function Async\spawn;

$sleeper = spawn(function () {
    try {
        time_nanosleep(PHP_INT_MAX, 0);
        echo "woke\n";
    } catch (Async\AsyncCancellation $e) {
        echo "cancelled\n";
    }
});

spawn(function () use ($sleeper) {
    Async\delay(10);
    $sleeper->cancel();
});

Async\await($sleeper);
echo "end\n";
?>
--EXPECT--
cancelled
end
