--TEST--
exit() after the graceful shutdown started cancels the coroutines spawned since
--FILE--
<?php
use function Async\spawn;
use function Async\suspend;
use function Async\graceful_shutdown;
use Async\AsyncCancellation;

graceful_shutdown();

/* Spawned after the shutdown started: exit() cancels it. */
spawn(function () {
    try {
        suspend();
        suspend();
        echo "survived exit\n";
    } catch (AsyncCancellation $e) {
        echo "late: ", $e->getMessage(), "\n";
    }
});
spawn(function () {
    exit();
});
?>
--EXPECTF--
late: Graceful shutdown
