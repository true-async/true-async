--TEST--
Fiber::resume() in an output handler, after the deactivation, of a fiber whose coroutine a bailout left parked throws a FiberError (review section 6, scheduler item 2; dev/RFC-CHANGES.md 24)
--FILE--
<?php
use TrueAsync\Test;

$fiber = null;

ob_start(function ($buffer) use (&$fiber) {
    try {
        $fiber->resume();
        $result = "resumed";
    } catch (FiberError $error) {
        $result = "FiberError: " . $error->getMessage();
    }

    return $buffer . "handler: $result\n";
});

register_shutdown_function(function () use (&$fiber) {
    Test\add_throwing_finish_handler(Async\current_coroutine(), true);
    $fiber = new Fiber(function () {
        Fiber::suspend();
        echo "not reached\n";
    });
    $fiber->start();
    echo "shutdown function end\n";
});

echo "main end\n";
?>
--EXPECTF--
main end
shutdown function end

Fatal error: finish handler of coroutine %d bails out in Unknown on line 0
handler: FiberError: Cannot switch fibers in current execution context
