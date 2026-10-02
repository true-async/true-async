--TEST--
exit() in a spawned coroutine ends the request gracefully: the other coroutines and main get the shutdown cancellation, and no exception is reported
--FILE--
<?php
use function Async\spawn;
use function Async\await;
use function Async\suspend;
use Async\AsyncCancellation;

$other = spawn(function () {
    try {
        suspend();
        suspend();
    } catch (AsyncCancellation $e) {
        echo "other: ", $e->getMessage(), "\n";
    } finally {
        echo "other finally\n";
    }
});
$exiting = spawn(function () {
    /* exit() runs no finally block, in a coroutine as in plain PHP. */
    try {
        echo "exiting\n";
        exit(3);
    } finally {
        echo "exiting finally\n";
    }
});
try {
    await($exiting);
} catch (AsyncCancellation $e) {
    echo "main: ", get_class($e), " ", $e->getMessage(), "\n";
} finally {
    echo "main finally\n";
}
echo "after\n";
?>
--EXPECTF--
exiting
other: Graceful shutdown
other finally
main: Async\AsyncCancellation Graceful shutdown
main finally
after
