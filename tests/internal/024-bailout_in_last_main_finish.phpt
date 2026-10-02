--TEST--
A bailout out of main's finish in the last from_main call leaves a parked coroutine and the scheduler coroutine parked in its loop to the request's end, which releases them (U6)
--INI--
true_async.debug_deadlock=0
--FILE--
<?php
use function Async\spawn;
use function Async\await;
use function Async\current_coroutine;
use TrueAsync\Test;

register_shutdown_function(function () {
    $main = current_coroutine();
    Test\add_throwing_finish_handler($main, true);

    // Main and the coroutine wait for each other: the scheduler coroutine resolves the deadlock
    // and switches into main, and stays parked in its loop.
    $parked = spawn(function () use ($main) {
        echo "coroutine waits for main\n";
        await($main);
        echo "not reached\n";
    });

    try {
        await($parked);
    } catch (Async\AsyncCancellation $cancellation) {
        echo "main: ", $cancellation->getMessage(), "\n";
    }

    echo "shutdown function end\n";
});

echo "main end\n";
?>
--EXPECTF--
main end
coroutine waits for main
main: Deadlock detected
shutdown function end

Fatal error: finish handler of coroutine %d bails out in Unknown on line 0
