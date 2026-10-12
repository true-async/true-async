--TEST--
A wait of the IO queue that fails ends the request: the coroutines are cancelled as by exit(), and the failure is reported
--FILE--
<?php
use TrueAsync\Test;

$sleeper = Async\spawn(function () {
    try {
        Async\delay(1000);
        echo "not reached\n";
    } catch (Async\AsyncCancellation $cancellation) {
        echo "sleeper: ", $cancellation->getMessage(), "\n";
    }
});

Test\fail_io('wait');

try {
    Async\await($sleeper);
} catch (Async\AsyncCancellation $cancellation) {
    echo "main: ", $cancellation->getMessage(), "\n";
}

echo "end\n";
?>
--EXPECTF--
sleeper: Graceful shutdown
main: Graceful shutdown
end

Fatal error: Uncaught Error: The IO queue's wait failed: %s in %s:%d
Stack trace:
#0 %s(%d): Async\delay(1000)
#1 [internal function]: {closure:%s}()
#2 {main}
  thrown in %s on line %d
