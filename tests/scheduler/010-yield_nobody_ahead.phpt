--TEST--
A yield with nobody ahead in the run queue runs on with no switch (S3.md 4.2, B3)
--FILE--
<?php
use function Async\spawn;
use function Async\suspend;
use function Async\current_coroutine;

spawn(function () {
    // Main has finished and nothing else is queued: the yield finds only this coroutine.
    echo "before\n";
    suspend();
    $coroutine = current_coroutine();
    var_dump($coroutine->isRunning(), $coroutine->isSuspended(), $coroutine->isQueued());
    suspend();
    echo "end\n";
});

echo "main\n";
?>
--EXPECT--
main
before
bool(true)
bool(false)
bool(false)
end
