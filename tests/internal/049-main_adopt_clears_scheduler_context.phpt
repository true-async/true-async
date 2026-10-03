--TEST--
A bailout out of main's finish handler leaves the scheduler-context flag set; the main minted after it runs outside scheduler context, so a shutdown function can spawn and await
--FILE--
<?php
use function Async\spawn;
use function Async\await;
use function Async\current_coroutine;
use TrueAsync\Test;

register_shutdown_function(function () {
    echo "shutdown function\n";
    echo "awaited: ", await(spawn(fn() => "spawned")), "\n";
});

// The handler runs in main's notify, in scheduler context on the OS stack, and bails out of it.
Test\add_throwing_finish_handler(current_coroutine(), true);
echo "main end\n";
?>
--EXPECTF--
main end

Fatal error: finish handler of coroutine %d bails out in Unknown on line 0
shutdown function
awaited: spawned
