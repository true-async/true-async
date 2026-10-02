--TEST--
Async\suspend() inside a Fiber the scheduler did not adopt is refused and leaves the coroutine running
--FILE--
<?php
use function Async\spawn;
use function Async\suspend;

spawn(function () {
    echo "coroutine\n";
});

$fiber = new Fiber(function () {
    try {
        suspend();
    } catch (Error $error) {
        echo $error->getMessage(), "\n";
    }

    echo "fiber end\n";
});

$fiber->start();
var_dump($fiber->isTerminated());
echo "main end\n";
?>
--EXPECT--
Cannot switch coroutines in the current execution context
fiber end
bool(true)
main end
coroutine
