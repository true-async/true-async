--TEST--
Two targets that finish in one pass of the queue wake their own waiters, and a coroutine awaits two targets in turn
--FILE--
<?php
use function Async\spawn;
use function Async\await;
use function Async\suspend;

$first = spawn(function () {
    suspend();
    return "first";
});

$second = spawn(function () {
    suspend();
    return "second";
});

spawn(function () use ($first) {
    echo "waiter 1: " . await($first) . "\n";
});

spawn(function () use ($second) {
    echo "waiter 2: " . await($second) . "\n";
});

$third = spawn(function () {
    suspend();
    suspend();
    return "third";
});

echo "main: " . await($first) . "\n";
echo "main: " . await($third) . "\n";
?>
--EXPECT--
main: first
waiter 1: first
waiter 2: second
main: third
