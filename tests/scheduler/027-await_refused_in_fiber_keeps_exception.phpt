--TEST--
Async\await() inside a Fiber waits for the target and throws its exception there; caught, it is observed and does not end the request (every Fiber is adopted since S3.9)
--FILE--
<?php
use function Async\spawn;
use function Async\await;

$coroutine = spawn(fn() => throw new Exception("boom"));

$fiber = new Fiber(function () use ($coroutine) {
    try {
        await($coroutine);
    } catch (Exception $exception) {
        echo "caught: ", $exception->getMessage(), "\n";
    }

    return "fiber end";
});

$fiber->start();
echo $fiber->getReturn(), "\n";
unset($fiber, $coroutine);
echo "end\n";
?>
--EXPECT--
caught: boom
fiber end
end
