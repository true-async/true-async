--TEST--
Three coroutines awaiting one coroutine all get its result; the wake order is TrueAsync's (the first, then the last, as a woken record leaves the vector)
--FILE--
<?php
use function Async\spawn;
use function Async\await;
use function Async\suspend;

$target = spawn(function () {
    suspend();
    return 42;
});

spawn(function () use ($target) {
    echo "a: " . await($target) . "\n";
});

spawn(function () use ($target) {
    echo "b: " . await($target) . "\n";
});

echo "main: " . await($target) . "\n";
?>
--EXPECT--
main: 42
b: 42
a: 42
