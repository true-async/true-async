--TEST--
A cancelled waiter holding the only reference to its target: the target's exception ends the request
--FILE--
<?php
use function Async\spawn;
use function Async\await;
use function Async\suspend;
use Async\AsyncCancellation;

/* The cancelled waiter held the only reference: the exception ends the request. */
$waiter = spawn(function () {
    $inner = spawn(function () {
        suspend();
        throw new RuntimeException("inner failed");
    });
    try {
        await($inner);
    } catch (AsyncCancellation $e) {
        echo "waiter: ", $e->getMessage(), "\n";
    }
});
suspend();
$waiter->cancel(new AsyncCancellation("waiter cancelled"));
echo "main end\n";
?>
--EXPECTF--
main end
waiter: waiter cancelled

Fatal error: Uncaught RuntimeException: inner failed in %s:%d
Stack trace:
#0 [internal function]: {closure:{closure:%s:%d}:%d}()
#1 {main}
  thrown in %s on line %d
