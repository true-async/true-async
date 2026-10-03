--TEST--
More coroutines end in one pass than the fiber context pool keeps: the contexts beyond its size are freed, and every coroutine runs to its end
--FILE--
<?php
use function Async\spawn;
use function Async\suspend;

$ended = 0;

for ($i = 0; $i < 1100; $i++) {
    spawn(function () use (&$ended) {
        suspend();
        $ended++;
    });
}

suspend();
suspend();
echo "ended: $ended\n";
?>
--EXPECT--
ended: 1100
