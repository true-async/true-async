--TEST--
An await refused inside a Fiber the scheduler did not adopt leaves the target's exception unobserved: it still ends the request
--FILE--
<?php
use function Async\spawn;
use function Async\await;

$coroutine = spawn(fn() => throw new Exception("boom"));

$fiber = new Fiber(function () use ($coroutine) {
    try {
        await($coroutine);
    } catch (Error $error) {
        echo $error->getMessage(), "\n";
    }
});

$fiber->start();
unset($fiber, $coroutine);
echo "end\n";
?>
--EXPECTF--
Cannot switch coroutines in the current execution context
end

Fatal error: Uncaught Exception: boom in %s:%d
%A
