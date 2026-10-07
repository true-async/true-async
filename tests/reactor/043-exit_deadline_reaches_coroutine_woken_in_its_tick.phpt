--TEST--
D16's graceful exit that finds a coroutine woken in its own suspend's tick unwinds it there: its finally runs, the code after the wait does not, and no exit object becomes its outcome
--FILE--
<?php
use function Async\{spawn, suspend};
use TrueAsync\Test;

Test\set_exit_deadline(300);

$coroutine = spawn(function () {
    try {
        suspend();
        suspend();
    } catch (Async\AsyncCancellation $e) {
        echo "caught: ", $e->getMessage(), "\n";
    }

    // Past the deadline with no poll, so D16 fires in the poll whose trigger walk wakes this coroutine.
    $end = hrtime(true) + 1000e6;
    while (hrtime(true) < $end) {
    }

    Test\trigger_new();
    Test\trigger_fire();

    try {
        Test\trigger_wait();
        echo "ran on after the wait\n";
    } finally {
        echo "finally\n";
    }
});

register_shutdown_function(function () use ($coroutine) {
    $outcome = $coroutine->getException();
    echo "outcome: ", $outcome === null ? "none" : get_class($outcome), "\n";
});

spawn(function () {
    exit(0);
});
?>
--EXPECT--
caught: Graceful shutdown
finally
outcome: none
