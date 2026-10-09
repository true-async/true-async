--TEST--
Fiber: exit() in a coroutine that runs between start() and the body cancels the body; a start() in the starter's finally throws FiberError
--FILE--
<?php
use function Async\spawn;

$fiber = new Fiber(function () {
    echo "body\n";
});

spawn(function () use ($fiber) {
    try {
        $fiber->start();
    } finally {
        try {
            $fiber->start();
            echo "second start returned\n";
        } catch (FiberError $e) {
            echo $e->getMessage(), "\n";
        }
    }
});

spawn(function () {
    echo "exit\n";
    exit(0);
});
?>
--EXPECT--
exit
Cannot start a fiber that has already been started
