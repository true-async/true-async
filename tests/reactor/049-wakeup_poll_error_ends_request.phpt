--TEST--
A wakeup poll that completes with an error ends the request: no trigger could wake anything any more
--FILE--
<?php
use TrueAsync\Test;

Test\trigger_new();

$waiter = Async\spawn(function () {
    try {
        Test\trigger_wait();
        echo "not reached\n";
    } catch (Async\AsyncCancellation $cancellation) {
        echo "waiter: ", $cancellation->getMessage(), "\n";
    }
});

Async\suspend();
Test\fail_io('complete');
Test\trigger_fire();

try {
    Async\await($waiter);
} catch (Async\AsyncCancellation $cancellation) {
    echo "main: ", $cancellation->getMessage(), "\n";
}

echo "end\n";
?>
--EXPECTF--
main: Graceful shutdown
end
waiter: Graceful shutdown

Fatal error: Uncaught Error: The wakeup's poll ended with status %d: %s in [no active file]:0
Stack trace:
#0 {main}
  thrown in [no active file] on line 0
