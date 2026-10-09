--TEST--
A resume() from another coroutine after the fiber yielded, before its waiting caller took the value, throws a FiberError; the caller gets what the fiber yielded
--FILE--
<?php
use function Async\spawn;
use function Async\await;

$fiber = new Fiber(function () {
    $value = Fiber::suspend('first yield');
    echo "body got $value\n";
    Fiber::suspend('second yield');
});
$fiber->start();

$first = spawn(function () use ($fiber) {
    $value = $fiber->resume('from first');
    echo "first got $value\n";
});

$second = spawn(function () use ($fiber) {
    Async\suspend();

    try {
        $fiber->resume('from second');
        echo "second resumed\n";
    } catch (FiberError $error) {
        echo "second: ", $error->getMessage(), "\n";
    }
});

await($first);
await($second);
?>
--EXPECT--
body got from first
second: Cannot resume a fiber that is not suspended
first got second yield
