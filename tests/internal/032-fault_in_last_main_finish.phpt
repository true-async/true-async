--TEST--
A fatal error when main's finish in the last from_main call wakes a parked waiter: the scheduler is not entered again, and the request's end unlinks the waiter's record (U6)
--FILE--
<?php
use function Async\spawn;
use function Async\await;
use function Async\current_coroutine;
use TrueAsync\Test;

register_shutdown_function(function () {
    $main = current_coroutine();

    spawn(function () use ($main) {
        echo "waiter awaits main\n";
        await($main);
        echo "not reached: waiter\n";
    });

    Async\suspend();
    // Main's finish in the last from_main call wakes the parked waiter, and the enqueue fails there:
    // the scheduler is never entered again, and the request's end unlinks the waiter's record.
    Test\fail_at('enqueue');
    echo "shutdown function end\n";
});

echo "main end\n";
?>
--EXPECTF--
main end
waiter awaits main
shutdown function end

Fatal error: Fault injected at enqueue in %s on line %d
