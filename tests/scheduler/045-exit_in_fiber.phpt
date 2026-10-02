--TEST--
exit() in an adopted fiber ends the request gracefully (D16): nothing is thrown out of the fiber, and its caller and the other coroutines get the shutdown's cancellation
--FILE--
<?php
use function Async\spawn;
use function Async\suspend;
use Async\AsyncCancellation;

register_shutdown_function(function () {
    echo "shutdown function\n";
});

spawn(function () {
    try {
        suspend();
        suspend();
    } catch (AsyncCancellation $cancellation) {
        echo "coroutine: ", $cancellation->getMessage(), "\n";
    }
});

$fiber = new Fiber(function () {
    /* exit() runs no finally block, in a fiber as in plain PHP. */
    try {
        echo "fiber exits\n";
        exit(3);
    } finally {
        echo "fiber finally\n";
    }
});

try {
    $fiber->start();
    echo "not reached\n";
} catch (AsyncCancellation $cancellation) {
    echo "main: ", get_class($cancellation), " ", $cancellation->getMessage(), "\n";
}

var_dump($fiber->isTerminated());
echo "main end\n";
?>
--EXPECT--
fiber exits
coroutine: Graceful shutdown
main: Async\AsyncCancellation Graceful shutdown
bool(true)
main end
shutdown function
