--TEST--
start() of a fiber whose starter was cancelled while the body waited in the queue throws a FiberError; the body runs once
--FILE--
<?php
use function Async\spawn;

$fiber = new Fiber(function () {
    echo "body\n";
    Fiber::suspend();
});

$starter = spawn(function () use ($fiber) {
    $fiber->start();
});

Async\suspend();
$starter->cancel();

try {
    $fiber->start();
    echo "second start returned\n";
} catch (FiberError $error) {
    echo $error->getMessage(), "\n";
}

Async\suspend();
?>
--EXPECT--
Cannot start a fiber that has already been started
body
