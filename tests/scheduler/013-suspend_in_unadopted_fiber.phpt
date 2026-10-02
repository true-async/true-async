--TEST--
Async\suspend() inside a Fiber parks the fiber's coroutine: the queued coroutine runs, then the fiber goes on (every Fiber is adopted since S3.9)
--FILE--
<?php
use function Async\spawn;
use function Async\suspend;

spawn(function () {
    echo "coroutine\n";
});

$fiber = new Fiber(function () {
    suspend();
    echo "fiber end\n";
});

$fiber->start();
var_dump($fiber->isTerminated());
echo "main end\n";
?>
--EXPECT--
coroutine
fiber end
bool(true)
main end
