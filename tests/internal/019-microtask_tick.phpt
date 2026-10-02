--TEST--
The tick runs deferred microtasks in scheduler context, skips a cancelled one, stops at the first throw, which ends the request gracefully, and runs the rest at the next tick
--FILE--
<?php
use function Async\spawn;
use function Async\suspend;
use TrueAsync\Test;

spawn(function () {
    Test\defer('A');
    Test\defer('B', 'throw');
    Test\defer('C', 'cancel');
    Test\defer('D');
    echo "suspend 1\n";

    /* B's exception starts the graceful shutdown, which cancels this coroutine. */
    try {
        suspend();
    } catch (Async\AsyncCancellation $e) {
        echo $e->getMessage(), "\n";
    }

    echo "suspend 2\n";
    suspend();
    echo "end\n";
});
?>
--EXPECTF--
suspend 1
microtask A sched=1
released A
microtask B sched=1
released B
Graceful shutdown
suspend 2
released C
microtask D sched=1
released D
end

Fatal error: Uncaught Exception: microtask B in %s:%d
Stack trace:
#0 %A
  thrown in %s on line %d
