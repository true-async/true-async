--TEST--
A wakeup poll the IO queue refuses to take again after its completion ends the request: no trigger could wake anything any more
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
Test\fail_io('submit');
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

Fatal error: Uncaught Error: Cannot submit an IO operation: %s in [no active file]:0
Stack trace:
#0 {main}
  thrown in [no active file] on line 0
