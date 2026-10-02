--TEST--
A waiter whose record a throwing finish handler left behind is woken by the teardown with the target's result, and the handler's exception ends the request
--FILE--
<?php
use function Async\spawn;
use function Async\await;
use function Async\suspend;
use TrueAsync\Test;

$target = spawn(function () {
    suspend();
    return "result";
});

/* Ahead of the waiter's record in the target's callbacks. */
Test\add_throwing_finish_handler($target);

spawn(function () use ($target) {
    echo "waiter: ", await($target), "\n";
});

echo "main end\n";
?>
--EXPECTF--
main end
waiter: result

Fatal error: Uncaught Exception: finish handler in %s:%d
Stack trace:
#0 {main}
  thrown in %s on line %d
