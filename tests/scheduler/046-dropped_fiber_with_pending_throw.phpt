--TEST--
A Fiber dropped while a throw() into it waits in the queue is closed by its graceful exit: the pending exception goes, and nothing is chained onto the exit
--FILE--
<?php
use function Async\spawn;
use Async\AsyncCancellation;

$holder = new stdClass();
$holder->fiber = new Fiber(function () {
    try {
        Fiber::suspend();
        echo "not reached\n";
    } catch (Throwable $throwable) {
        echo "not reached either: ", get_class($throwable), "\n";
    } finally {
        echo "fiber finally\n";
    }
});
$holder->fiber->start();

/* The caller queues the throw and waits; it is cancelled ahead of the fiber and drops it. */
$caller = spawn(function () use ($holder) {
    try {
        $holder->fiber->throw(new Exception("thrown"));
        echo "not reached\n";
    } catch (AsyncCancellation $cancellation) {
        echo "caller cancelled\n";
    }

    $holder->fiber = null;
    echo "caller dropped the fiber\n";
});

spawn(function () use ($caller) {
    $caller->asHiPriority();
    $caller->cancel();
});

echo "main end\n";
?>
--EXPECT--
main end
caller cancelled
caller dropped the fiber
fiber finally
