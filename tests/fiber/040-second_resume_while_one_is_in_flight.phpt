--TEST--
A second resume() of a fiber whose first resume() has not come back yet throws a FiberError; the first resumer gets the fiber's next suspend
--FILE--
<?php
use function Async\spawn;
use function Async\await;

$fiber = new Fiber(function () {
    $value = Fiber::suspend();
    echo "body got $value\n";
    Fiber::suspend();
});
$fiber->start();

$first = spawn(function () use ($fiber) {
    $fiber->resume('first');
    echo "first back\n";
});

$second = spawn(function () use ($fiber) {
    try {
        $fiber->resume('second');
        echo "second back\n";
    } catch (FiberError $error) {
        echo "second: ", $error->getMessage(), "\n";
    }
});

await($first);
await($second);
?>
--EXPECT--
second: Cannot resume a fiber that is not suspended
body got first
first back
