--TEST--
A wait of the IO queue that keeps failing ends the request once: a wait after the first failure is resolved as a deadlock, with no new error and no spin of the idle loop
--INI--
true_async.debug_deadlock=0
--FILE--
<?php
use TrueAsync\Test;

// Started before the fault: a coroutine the shutdown finds unstarted never runs its finally.
$started = new Async\FutureState();
Async\spawn(function () use ($started) {
    $started->complete(null);

    try {
        Async\delay(100000);
        echo "not reached\n";
    } finally {
        echo "finally\n";

        try {
            Async\delay(100000);
            echo "not reached\n";
        } catch (Async\AsyncCancellation $cancellation) {
            echo "wait in finally: ", get_class($cancellation), "\n";
        }
    }
});

(new Async\Future($started))->await();
Test\fail_io('wait_always');
// The first failing wait is the idle one, or main's own poll of its park when 100 ms have passed since the
// last poll; the Error then carries main's frame, so both locations are expected.
Async\delay(100000);
echo "not reached\n";
?>
--EXPECTF--
finally
wait in finally: %s

Fatal error: Uncaught Error: The IO queue's wait failed: %s in %s:%d
Stack trace:
#0 %A{main}

Next Async\DeadlockError: Deadlock detected: no active coroutines, 1 coroutines in waiting in [no active file]:0
Stack trace:
#0 {main}
  thrown in [no active file] on line 0
