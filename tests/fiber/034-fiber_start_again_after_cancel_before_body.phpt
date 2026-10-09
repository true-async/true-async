--TEST--
Fiber: start() again after the fiber's coroutine was cancelled before its body ran throws FiberError
--FILE--
<?php
use function Async\spawn;
use function Async\await;
use Async\AsyncCancellation;

$fiber = new Fiber(function () {
    echo "body\n";
});

$starter = spawn(function () use ($fiber) {
    try {
        $fiber->start();
    } catch (AsyncCancellation $e) {
        echo "start threw\n";
    }
});

spawn(function () use ($fiber) {
    $fiber->getCoroutine()->cancel();
});

await($starter);

try {
    $fiber->start();
    echo "second start returned\n";
} catch (FiberError $e) {
    echo $e->getMessage(), "\n";
}
?>
--EXPECT--
start threw
Cannot start a fiber that has already been started
